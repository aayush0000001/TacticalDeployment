// Copyright TacticalDeployment. All Rights Reserved.

#include "SimTest.h"

#include <cstdarg>

namespace SimTest
{
	std::vector<FTestCase>& Registry()
	{
		static std::vector<FTestCase> Tests;
		return Tests;
	}

	int GFailuresInCurrentTest = 0;
	std::vector<std::string> GCurrentReport;

	void RecordFailure(const char* File, int Line, const std::string& Message)
	{
		++GFailuresInCurrentTest;
		std::printf("      FAIL %s:%d: %s\n", File, Line, Message.c_str());
	}

	std::string Format(const char* Fmt, ...)
	{
		if (!Fmt || !*Fmt)
		{
			return std::string();
		}
		char Buffer[1024];
		va_list Args;
		va_start(Args, Fmt);
		std::vsnprintf(Buffer, sizeof(Buffer), Fmt, Args);
		va_end(Args);
		return Buffer;
	}

	void Report(const char* Fmt, ...)
	{
		char Buffer[1024];
		va_list Args;
		va_start(Args, Fmt);
		std::vsnprintf(Buffer, sizeof(Buffer), Fmt, Args);
		va_end(Args);
		GCurrentReport.push_back(Buffer);
	}
}
