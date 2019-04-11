// By default, perf is disabled, but enable running tests with it disabled by locally
// undefing if needed.
#ifdef ENVOY_PERF_ANNOTATION
#undef ENVOY_PERF_ANNOTATION
#endif

#include "common/common/perf_annotation.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(PerfAnnotationDisabled, testPerfAnnotation) {
  PERF_OPERATION(perf);
  PERF_RECORD(perf, "alpha", "0");
  PERF_RECORD(perf, "beta", "1");
  PERF_RECORD(perf, "alpha", "2");
  PERF_RECORD(perf, "beta", "3");
  std::string report = PERF_TO_STRING();
  EXPECT_TRUE(report.empty());
  PERF_CLEAR();
}

} // namespace Envoy
