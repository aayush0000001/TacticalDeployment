// Copyright TacticalDeployment. All Rights Reserved.
// Tiny self-contained test runner for the simulation harness (no external dependencies).

#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace SimTest
{
	struct FTestCase
	{
		const char* Suite;
		const char* Name;
		void (*Fn)();
	};

	std::vector<FTestCase>& Registry();

	struct FRegistrar
	{
		FRegistrar(const char* Suite, const char* Name, void (*Fn)()) { Registry().push_back({ Suite, Name, Fn }); }
	};

	/** Failure recorded against the currently running test. */
	void RecordFailure(const char* File, int Line, const std::string& Message);

	/** A measured value worth reading in the report (printed under the test name). */
	void Report(const char* Format, ...);

	std::string Format(const char* Format, ...);
}

#define SIM_TEST(Suite, Name) \
	static void SimTest_##Suite##_##Name(); \
	static SimTest::FRegistrar SimTestRegistrar_##Suite##_##Name(#Suite, #Name, &SimTest_##Suite##_##Name); \
	static void SimTest_##Suite##_##Name()

#define SIM_EXPECT(Cond, ...) \
	do { if (!(Cond)) { SimTest::RecordFailure(__FILE__, __LINE__, std::string("EXPECT(" #Cond ") ") + SimTest::Format(__VA_ARGS__)); } } while (0)

#define SIM_EXPECT_TRUE(Cond) SIM_EXPECT(Cond, "")
#define SIM_EXPECT_FALSE(Cond) SIM_EXPECT(!(Cond), "")

#define SIM_EXPECT_NEAR(Actual, Expected, Tolerance) \
	do { const double SimA_ = double(Actual), SimE_ = double(Expected), SimT_ = double(Tolerance); \
		if (!(SimA_ <= SimE_ + SimT_ && SimA_ >= SimE_ - SimT_)) { \
			SimTest::RecordFailure(__FILE__, __LINE__, SimTest::Format("NEAR(%s): got %.6f, expected %.6f +/- %.6f", #Actual, SimA_, SimE_, SimT_)); } } while (0)

#define SIM_EXPECT_EQ(Actual, Expected) \
	do { const auto SimA_ = (Actual); const auto SimE_ = (Expected); \
		if (!(SimA_ == SimE_)) { SimTest::RecordFailure(__FILE__, __LINE__, SimTest::Format("EQ(%s): got %lld, expected %lld", #Actual, (long long)SimA_, (long long)SimE_)); } } while (0)

#define SIM_EXPECT_LE(A, B) SIM_EXPECT(double(A) <= double(B), "%s = %.6f, %s = %.6f", #A, double(A), #B, double(B))
#define SIM_EXPECT_LT(A, B) SIM_EXPECT(double(A) < double(B), "%s = %.6f, %s = %.6f", #A, double(A), #B, double(B))
#define SIM_EXPECT_GE(A, B) SIM_EXPECT(double(A) >= double(B), "%s = %.6f, %s = %.6f", #A, double(A), #B, double(B))
#define SIM_EXPECT_GT(A, B) SIM_EXPECT(double(A) > double(B), "%s = %.6f, %s = %.6f", #A, double(A), #B, double(B))
