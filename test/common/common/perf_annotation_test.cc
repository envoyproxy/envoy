// When building from the command line, you can enable perf annotation globally with
//   bazel --define=perf_annotation=enabled
// You can also do this in on a per-file basis with by defining the macro manually. You
// must be sure to do this in the modules where you are collecting the perf annotations
// (PERF_OPERATION, PERF_RECORD) and also where you are reporting them (PERF_DUMP).
#ifndef ENVOY_PERF_ANNOTATION
#define ENVOY_PERF_ANNOTATION
#endif

#include "common/common/perf_annotation.h"

#include "gtest/gtest.h"

namespace Envoy {

class PerfAnnotationTest : public testing::Test {
protected:
  void TearDown() override { PERF_CLEAR(); }
};

// Tests that the macros produce something in the report that includes the categories
// and descriptions.
TEST_F(PerfAnnotationTest, testMacros) {
  PERF_OPERATION(perf);
  PERF_RECORD(perf, "alpha", "0");
  PERF_RECORD(perf, "beta", "1");
  PERF_RECORD(perf, "alpha", "2");
  PERF_RECORD(perf, "beta", "3");
  std::string report = PERF_TO_STRING();
  EXPECT_TRUE(report.find(" alpha ") != std::string::npos) << report;
  EXPECT_TRUE(report.find(" 0\n") != std::string::npos) << report;
  EXPECT_TRUE(report.find(" beta ") != std::string::npos) << report;
  EXPECT_TRUE(report.find(" 1\n") != std::string::npos) << report;
  EXPECT_TRUE(report.find(" alpha ") != std::string::npos) << report;
  EXPECT_TRUE(report.find(" 2\n") != std::string::npos) << report;
  EXPECT_TRUE(report.find(" beta ") != std::string::npos) << report;
  EXPECT_TRUE(report.find(" 3\n") != std::string::npos) << report;
  PERF_DUMP();
}

// More detailed report-format testing, directly using the class.
TEST_F(PerfAnnotationTest, testFormat) {
  PerfAnnotationContext* context = PerfAnnotationContext::getOrCreate();
  for (int i = 0; i < 4; ++i) {
    context->record(std::chrono::microseconds{1000 + 100 * i}, "alpha", "1");
  }
  for (int i = 0; i < 3; ++i) {
    context->record(std::chrono::microseconds{30 - i}, "beta", "3");
  }
  context->record(std::chrono::microseconds{200}, "gamma", "2");
  std::string report = context->toString();
  EXPECT_EQ(
      "Duration(us)  # Calls  Mean(ns)  StdDev(ns)  Min(ns)  Max(ns)  Category  Description\n"
      "        4600        4   1150000      129099  1000000  1300000     alpha            1\n"
      "         200        1    200000         nan   200000   200000     gamma            2\n"
      "          87        3     29000        1000    28000    30000      beta            3\n",
      context->toString());
}

} // namespace Envoy
