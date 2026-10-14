# Änderungen

## 0.1.0 – 2026-10-14

Die erste Version des Auto Mixers! 🎉 Er misst jede Audioquelle und jede Tonspur in OBS und gleicht sie automatisch und unauffällig an ein Preset an. Alles läuft lokal: keine Cloud, keine KI, keine Telemetrie.

### ✨ Neu

- **Messung wie ein Lautheitsmesser:** EBU R128 / ITU-R BS.1770 (Momentary, Short-term), True Peak mit 4-facher Überabtastung und Übersteuerung, pro Quelle und pro Tonspur 1–6. Der OBS-Fader wird mitgerechnet.
- **Rollen:** Stimme, Spiel, Chat, Musik, Alerts, Desktop oder Ignorieren. Sie werden aus Quellentyp, Name und Programm vorgeschlagen, z. B. `discord.exe` → Chat.
- **7 Presets:** Stimme vor Spiel, Spiel vor Stimme, Ausgeglichen, Competitive, Just Chatting, Musik und Gäste/Podcast, dazu eigene Presets mit Import und Export als JSON.
- **Preset „Automatisch“:** wählt nach Szene oder nach dem, was gerade passiert, ohne hektisch hin und her zu springen.
- **Regelung über einen eigenen Filter:** sample-genau geglättet, Totzone ±1 LU, leiser schnell, lauter langsam, in Stille nie lauter. Dein Fader bleibt deiner.
- **Ducking:** Sprichst du, geht das Spiel ein paar dB zurück und kommt danach sanft wieder.
- **Schreischutz und True-Peak-Limiter:** Schreie und Explosionen werden in rund 50 ms abgefangen, nichts übersteuert.
- **Langzeit-Lernen:** Eine einzelne Explosion verschiebt die Balance nicht, ein dauerhaft lauteres Spiel schon.
- **Warnungen:** zu laut, zu leise, übersteuert, Gesamtmix zu laut und „Du sprichst, aber dein Mikro ist stumm!“
- **Modus „Nur beobachten“:** misst und warnt, ändert aber nichts. Damit startet der Auto Mixer.
- **Einmessen in 30 Sekunden:** schlägt Startwerte und ein Preset vor und erkennt zu leise, rauschende oder übersteuerte Mikros.
- **Dock auf Deutsch und Englisch** mit Pegeln, Zielmarke, Status, Schloss je Quelle und Verlauf.
- **Tastenkürzel:** Preset 1–9, Automatisch, Pausieren/Fortsetzen, Neu einmessen.
- **Windows-Setup**, das OBS findet und das Plugin für alle Benutzer installiert.

### 📦 Installation

**Windows:** `obs-audio-mixer-0.1.0-windows-x64-setup.exe` herunterladen und starten, danach OBS öffnen. Das Dock „Auto Mixer“ erscheint rechts. Voraussetzung ist OBS Studio 31 oder neuer.

> [!NOTE]
> Das Setup ist noch nicht digital signiert. Meldet Windows „Der Computer wurde durch Windows geschützt“, klick auf **Weitere Informationen → Trotzdem ausführen**. Unter Windows 11 kann die *Intelligente App-Steuerung* unsignierte Plugins blockieren, siehe [README](https://github.com/BellocccYT/obs-audio-mixer#-installation-windows).

**macOS und Ubuntu:** Die Pakete werden automatisch gebaut, wurden aber noch nicht auf echten Geräten getestet. Rückmeldungen sind sehr willkommen!

Vor dem Deinstallieren bitte in OBS unter **Auto Mixer → Einstellungen → Allgemein** auf **„Filter von allen Quellen entfernen“** klicken.
