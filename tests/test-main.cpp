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

#include "test-framework.hpp"

#include <cstdio>
#include <cstring>

namespace test {

namespace {

int g_failures = 0;

} // namespace

std::vector<Case> &registry()
{
	static std::vector<Case> cases;
	return cases;
}

void fail(const char *file, int line, const std::string &message)
{
	g_failures++;
	std::printf("  FEHLER %s:%d: %s\n", file, line, message.c_str());
}

std::string format(double value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.3f", value);
	return buffer;
}

} // namespace test

int main(int argc, char **argv)
{
	const char *group = argc > 1 ? argv[1] : nullptr;
	int run = 0;
	for (const test::Case &c : test::registry()) {
		if (group && std::strcmp(group, c.group) != 0)
			continue;
		const int before = test::g_failures;
		std::printf("[%s] %s\n", c.group, c.name);
		c.run();
		std::printf("  %s\n", test::g_failures == before ? "ok" : "FEHLGESCHLAGEN");
		run++;
	}
	if (run == 0) {
		std::printf("Keine Tests für Gruppe '%s'\n", group ? group : "");
		return 1;
	}
	std::printf("%d Tests, %d Fehler\n", run, test::g_failures);
	return test::g_failures == 0 ? 0 : 1;
}
