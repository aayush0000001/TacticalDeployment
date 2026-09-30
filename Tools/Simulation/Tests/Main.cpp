// Copyright TacticalDeployment. All Rights Reserved.
//
// Usage: TacticalSimulation [SuiteOrTestSubstring]

#include "SimTest.h"

#include <cstring>

namespace SimTest
{
	extern int GFailuresInCurrentTest;
	extern std::vector<std::string> GCurrentReport;
}

int main(int argc, char** argv)
{
	const char* Filter = argc > 1 ? argv[1] : nullptr;
	int Passed = 0;
	int Failed = 0;
	const char* CurrentSuite = "";

	for (const SimTest::FTestCase& Test : SimTest::Registry())
	{
		const std::string FullName = std::string(Test.Suite) + "." + Test.Name;
		if (Filter && !std::strstr(FullName.c_str(), Filter))
		{
			continue;
		}
		if (std::strcmp(CurrentSuite, Test.Suite) != 0)
		{
			CurrentSuite = Test.Suite;
			std::printf("\n[%s]\n", CurrentSuite);
		}

		SimTest::GFailuresInCurrentTest = 0;
		SimTest::GCurrentReport.clear();
		Test.Fn();

		const bool bOk = SimTest::GFailuresInCurrentTest == 0;
		std::printf("  %s %s\n", bOk ? "PASS" : "FAIL", Test.Name);
		for (const std::string& Line : SimTest::GCurrentReport)
		{
			std::printf("      - %s\n", Line.c_str());
		}
		(bOk ? Passed : Failed)++;
	}

	std::printf("\n%d passed, %d failed\n", Passed, Failed);
	return Failed == 0 ? 0 : 1;
}
