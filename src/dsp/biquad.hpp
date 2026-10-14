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

#pragma once

#include <cmath>

namespace am::dsp {

// Koeffizienten eines Filters 2. Ordnung, normiert auf a0 = 1
struct BiquadCoeffs {
	double b0 = 1.0;
	double b1 = 0.0;
	double b2 = 0.0;
	double a1 = 0.0;
	double a2 = 0.0;
};

// Filter 2. Ordnung (Direktform II transponiert). Rechnet in double: der Hochpass der K-Gewichtung liegt
// sehr nah am Einheitskreis, float wäre bei tiefen Frequenzen zu ungenau.
class Biquad {
public:
	void setCoeffs(const BiquadCoeffs &coeffs) { c_ = coeffs; }
	const BiquadCoeffs &coeffs() const { return c_; }
	void reset() { z1_ = z2_ = 0.0; }

	double process(double x)
	{
		const double y = c_.b0 * x + z1_;
		z1_ = c_.b1 * x - c_.a1 * y + z2_;
		z2_ = c_.b2 * x - c_.a2 * y;
		return y;
	}

	// Winzige Restwerte nach Stille auf 0 setzen, sonst rechnet die CPU mit langsamen denormalen Zahlen
	void flushDenormals()
	{
		if (std::fabs(z1_) < 1e-20)
			z1_ = 0.0;
		if (std::fabs(z2_) < 1e-20)
			z2_ = 0.0;
	}

private:
	BiquadCoeffs c_;
	double z1_ = 0.0;
	double z2_ = 0.0;
};

} // namespace am::dsp
