#include "common/common/perf_annotation.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(PerfAnnotationDisabled, testPerfAnnotation) {
  PERF_OPERATION(perf);
  PERF_REPORT(perf, "alpha", "0");
  PERF_REPORT(perf, "beta", "1");
  PERF_REPORT(perf, "alpha", "2");
  PERF_REPORT(perf, "beta", "3");
  std::string report = PERF_TO_STRING();
  EXPECT_TRUE(report.empty());
  PERF_CLEAR();
}

} // namespace Envoy
