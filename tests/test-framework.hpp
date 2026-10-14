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

// Kleines Test-Gerüst ohne Abhängigkeiten. Jeder Test gehört zu einer Gruppe; CTest startet die Gruppen
// einzeln (automixer-dsp-tests <gruppe>).

#include <cmath>
#include <string>
#include <vector>

namespace test {

struct Case {
	const char *group;
	const char *name;
	void (*run)();
};

std::vector<Case> &registry();
void fail(const char *file, int line, const std::string &message);

struct Registrar {
	Registrar(const char *group, const char *name, void (*run)()) { registry().push_back({group, name, run}); }
};

std::string format(double value);

} // namespace test

#define TEST_CASE(group, name)                                                              \
	static void test_##name();                                                          \
	static const test::Registrar registrar_##name(group, #name, &test_##name); \
	static void test_##name()

#define CHECK(condition)                                               \
	do {                                                           \
		if (!(condition))                                      \
			test::fail(__FILE__, __LINE__, "CHECK(" #condition ")"); \
	} while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                              \
	do {                                                                                                 \
		const double actual_ = (actual);                                                             \
		const double expected_ = (expected);                                                         \
		if (!(std::fabs(actual_ - expected_) <= (tolerance)))                                         \
			test::fail(__FILE__, __LINE__,                                                       \
				   std::string(#actual) + " = " + test::format(actual_) + ", erwartet " +    \
					   test::format(expected_) + " ± " + test::format(tolerance));       \
	} while (0)

#define CHECK_RANGE(actual, low, high)                                                                        \
	do {                                                                                                  \
		const double actual_ = (actual);                                                              \
		if (!(actual_ >= (low) && actual_ <= (high)))                                                \
			test::fail(__FILE__, __LINE__,                                                        \
				   std::string(#actual) + " = " + test::format(actual_) + ", erwartet zwischen " + \
					   test::format(low) + " und " + test::format(high));                 \
	} while (0)
