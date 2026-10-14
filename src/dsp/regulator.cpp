/*
Auto Mixer
Copyright (C) 2026 Belloccc

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "regulator.hpp"

#include <algorithm>
#include <cmath>

namespace am::dsp {

namespace {

constexpr float kPresetReference = -16.0f; // auf diese Ziel-Lautheit sind die Presets ausgelegt
constexpr float kMinTotalGain = -60.0f;
constexpr float kReportLu = 2.0f; // Anpassungen ab dieser Größe landen im Verlauf
constexpr float kGuardReportDb = -3.0f;
constexpr float kNoLimit = 100.0f;
constexpr float kShortTermSeconds = 3.0f; // so lange muss eine Quelle aktiv sein, bis Short-term stimmt
constexpr float kWarmupSeconds = 0.5f;    // … und so lange, bis Momentary stimmt
constexpr float kOutlierLu = 6.0f;        // so weit über dem Gelernten ist ein Wert ein Ausreißer
constexpr float kLearnClipLu = 3.0f;      // pro Wert lernt der Regler höchstens so viel Abweichung
constexpr float kRelearnLouderSeconds = 6.0f;
constexpr float kRelearnQuieterSeconds = 10.0f;
constexpr float kRelearnSeconds = 10.0f; // Zeitkonstante beim schnellen Nachlernen
constexpr float kGuardReleaseTau = 1.0f; // der Schreischutz löst sich etwa mit dieser Zeitkonstante

// Bewegt value mit höchstens rate · dt auf target zu
float approach(float value, float target, float rate, float dt)
{
	const float step = rate * dt;
	return value + std::clamp(target - value, -step, step);
}

// Wie lange die Lautheit einer Rolle gemittelt wird (Sekunden)
float learnSeconds(Role role, const SpeedProfile &profile)
{
	switch (role) {
	case Role::Voice:
	case Role::Chat:
		return 4.0f; // der Sprachpegel ist schon über die letzten 3 s Sprache gemittelt
	case Role::Music:
		return 20.0f;
	case Role::Alerts:
		return 8.0f;
	default:
		return profile.learnSeconds; // Spiel und Desktop: über Minuten, damit Explosionen nichts verschieben
	}
}

} // namespace

float Regulator::target(Role role) const
{
	if (role == Role::Ignore)
		return kSilence;
	return preset_.targetLufs + preset_.offset(role) + (config_.referenceLufs - kPresetReference);
}

void Regulator::update(double dt, Channel *const *channels, size_t count)
{
	const float step = static_cast<float>(std::clamp(dt, 0.0, 1.0));

	// Ducking: spricht gerade jemand, dessen Stimme Vorrang hat?
	bool speech = false;
	for (size_t i = 0; i < count; i++) {
		const ChannelInput &in = channels[i]->in;
		if (in.present && in.audible && in.speaking && preset_.isDuckTrigger(in.role))
			speech = true;
	}
	if (speech)
		duckHold_ = std::max(preset_.duckHoldMs / 1000.0f, 0.05f);
	else
		duckHold_ = std::max(0.0f, duckHold_ - step);

	for (size_t i = 0; i < count; i++)
		updateChannel(step, *channels[i], ducking());
}

void Regulator::updateChannel(float dt, Channel &channel, bool duck) const
{
	const ChannelInput &in = channel.in;
	ChannelState &s = channel.state;
	const SpeedProfile &profile = speedProfile(preset_.speed);
	s.events = 0;
	s.eventDb = 0.0f;
	s.targetLufs = target(in.role);

	if (in.locked) {
		// Gesperrt: alles bleibt, wie es ist
		s.active = false;
		s.moving = false;
		return;
	}

	if (in.role == Role::Ignore) {
		// Nicht zuständig: sanft zurück auf 0 dB
		s.levelGain = approach(s.levelGain, 0.0f, profile.upDbPerS, dt);
		s.duckGain = approach(s.duckGain, 0.0f, profile.duckReleaseDbPerS, dt);
		s.guardGain = approach(s.guardGain, 0.0f, profile.guardReleaseDbPerS, dt);
		s.guardLimit = kNoLimit;
		s.gainDb = s.levelGain + s.duckGain + s.guardGain;
		s.active = false;
		s.activeRun = 0.0f;
		s.moving = false;
		s.ducking = false;
		return;
	}

	const bool speechRole = isSpeechRole(in.role);
	const float target = s.targetLufs;

	// Aktiv? Stimmen nur beim Sprechen, alles andere ab einer Mindestlautheit. In Stille wird nie geregelt,
	// sonst würde das Rauschen hochgezogen.
	float level = kSilence;
	if (in.present) {
		if (speechRole) {
			s.active = in.speaking;
			level = isSilent(in.speech) ? in.momentary : in.speech;
		} else {
			s.active = in.momentary > config_.activityGateLufs;
			// Direkt nach Stille enthält das 3-s-Fenster noch die Stille: bis dahin Momentary nehmen
			level = s.activeRun >= kShortTermSeconds ? in.shortTerm : in.momentary;
		}
	} else {
		s.active = false;
	}
	s.activeRun = s.active ? s.activeRun + dt : 0.0f;

	// Grundlautheit lernen. Die erste halbe Sekunde zählt nicht (das Momentary-Fenster enthält dann noch
	// Stille); danach erst ein einfacher Mittelwert, ab der Zeitkonstante ein gleitender. Ausreißer
	// (Schrei, Explosion) und ihr Nachhall in den 3-s-Fenstern zählen nicht. Bleibt es dagegen lange
	// deutlich lauter oder leiser, hat sich die Grundlautheit wirklich geändert: dann schnell nachlernen.
	const bool settled = speechRole || s.activeRun >= kWarmupSeconds;
	if (s.active && settled && !isSilent(level)) {
		s.activeSeconds += dt;
		if (isSilent(s.estimate)) {
			s.estimate = level;
		} else {
			const float instant = speechRole ? in.speechBlock : in.momentary;
			const float instantDeviation = isSilent(instant) ? 0.0f : instant - s.estimate;
			s.louderSeconds = instantDeviation > kOutlierLu ? s.louderSeconds + dt
									: std::max(0.0f, s.louderSeconds - 0.5f * dt);
			s.quieterSeconds = instantDeviation < -kOutlierLu
						   ? s.quieterSeconds + dt
						   : std::max(0.0f, s.quieterSeconds - 0.5f * dt);
			if (s.louderSeconds >= kRelearnLouderSeconds || s.quieterSeconds >= kRelearnQuieterSeconds)
				s.relearning = true;
			if (instantDeviation > kOutlierLu)
				s.outlierHold = kShortTermSeconds;
			else
				s.outlierHold = std::max(0.0f, s.outlierHold - dt);

			const float deviation = level - s.estimate;
			if (s.relearning) {
				s.estimate += std::min(1.0f, dt / kRelearnSeconds) * deviation;
				if (std::fabs(deviation) < 1.0f) {
					s.relearning = false;
					s.louderSeconds = s.quieterSeconds = 0.0f;
				}
			} else if (s.outlierHold <= 0.0f) {
				const float tau =
					std::min(learnSeconds(in.role, profile), static_cast<float>(s.activeSeconds));
				const float alpha = std::min(1.0f, dt / std::max(tau, dt));
				s.estimate += alpha * std::clamp(deviation, -kLearnClipLu, kLearnClipLu);
			}
		}
	}

	// Grundverstärkung: mit Totzone und Hysterese, leiser schneller als lauter
	if (!isSilent(s.estimate) && s.active) {
		const float desired = std::clamp(target - s.estimate - in.faderDb, in.minGainDb, in.maxGainDb);
		const float error = desired - s.levelGain;
		if (!s.moving && std::fabs(error) > config_.deadzoneLu)
			s.moving = true;
		if (s.moving) {
			s.levelGain = approach(s.levelGain, desired,
					       error < 0.0f ? profile.downDbPerS : profile.upDbPerS, dt);
			if (std::fabs(desired - s.levelGain) < config_.settleLu) {
				s.moving = false;
				const float change = s.levelGain - s.reportedLevel;
				if (std::fabs(change) >= kReportLu) {
					s.events |= change < 0.0f ? kEventLevelDown : kEventLevelUp;
					s.eventDb = change;
					s.reportedLevel = s.levelGain;
				}
			}
		}
	}

	// Ducking: der Rest wird leiser, solange gesprochen wird (plus Haltezeit)
	const bool ducked = duck && preset_.isDucked(in.role) && preset_.duckDb > 0.0f;
	if (ducked != s.ducking && preset_.duckDb >= 1.0f && preset_.isDucked(in.role)) {
		s.events |= ducked ? kEventDuckStart : kEventDuckEnd;
		s.eventDb = ducked ? -preset_.duckDb : preset_.duckDb;
	}
	s.ducking = ducked;
	const float duckTarget = ducked ? -preset_.duckDb : 0.0f;
	s.duckGain = approach(s.duckGain, duckTarget,
			      duckTarget < s.duckGain ? config_.duckAttackDbPerS : profile.duckReleaseDbPerS, dt);

	// Schreischutz: plötzlich weit über dem Ziel oder True Peak zu hoch → sofort eine Obergrenze für die
	// Gesamtverstärkung setzen. Als Obergrenze (statt zusätzlicher Absenkung) addiert er sich nicht zur
	// Grundregelung, wenn die gleichzeitig nach unten nachzieht.
	bool triggered = false;
	if (preset_.screamGuard && in.present) {
		const float fast = std::max(in.momentary, in.block - 2.0f);
		const float base = s.levelGain + s.duckGain + in.faderDb;
		if (!isSilent(fast) && fast + base > target + config_.screamLu) {
			s.guardLimit = std::min(s.guardLimit, target + 2.0f - fast - in.faderDb);
			triggered = true;
		}
		if (!isSilent(in.truePeak) && in.truePeak + base > config_.ceilingDbtp) {
			s.guardLimit = std::min(s.guardLimit, config_.ceilingDbtp - 0.5f - in.truePeak - in.faderDb);
			triggered = true;
		}
	}
	if (triggered) {
		s.guardHold = config_.guardHoldSeconds;
	} else if (s.guardHold > 0.0f) {
		s.guardHold = std::max(0.0f, s.guardHold - dt);
	} else if (s.guardLimit < kNoLimit) {
		// Lösen: große Absenkungen anfangs schneller, kleine mit Mindesttempo; liegt die Grenze über der
		// Verstärkung, wird sie aufgehoben
		const float gap = std::max(0.0f, s.levelGain + s.duckGain - s.guardLimit);
		s.guardLimit += std::max(profile.guardReleaseDbPerS * dt, gap * std::min(1.0f, dt / kGuardReleaseTau));
		if (s.guardLimit >= s.levelGain + s.duckGain + 1.0f)
			s.guardLimit = kNoLimit;
	}
	const float guard = std::min(0.0f, s.guardLimit - (s.levelGain + s.duckGain));
	if (s.guardGain > kGuardReportDb && guard <= kGuardReportDb) {
		s.events |= kEventGuard;
		s.eventDb = guard;
	}
	s.guardGain = guard;

	s.gainDb = std::clamp(s.levelGain + s.duckGain + s.guardGain, kMinTotalGain, in.maxGainDb);
}

} // namespace am::dsp
