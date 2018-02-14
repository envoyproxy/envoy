#ifndef ENVOY_PERF_ANNOTATION
#define ENVOY_PERF_ANNOTATION
#endif

#include "common/common/perf_annotation.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(PerfAnnotation, testPerfAnnotation) {
  PERF_OPERATION(perf);
  PERF_REPORT(perf, "alpha", "0");
  PERF_REPORT(perf, "beta", "1");
  PERF_REPORT(perf, "alpha", "2");
  PERF_REPORT(perf, "beta", "3");
  std::string report = PERF_TO_STRING();
  EXPECT_TRUE(report.find("alpha / 0\n") != std::string::npos) << report;
  EXPECT_TRUE(report.find("beta / 1\n") != std::string::npos) << report;
  EXPECT_TRUE(report.find("alpha / 2\n") != std::string::npos) << report;
  EXPECT_TRUE(report.find("beta / 3\n") != std::string::npos) << report;
  PERF_DUMP();
  PERF_CLEAR();
}

} // namespace Envoy
