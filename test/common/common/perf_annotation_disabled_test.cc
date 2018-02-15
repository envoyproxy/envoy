<<<<<<< HEAD
=======
// By default, perf is disabled, but enable running tests with it disabled by locally
// undefing if needed.
#ifdef ENVOY_PERF_ANNOTATION
#undef ENVOY_PERF_ANNOTATION
#endif

>>>>>>> perf-annotation-lib
#include "common/common/perf_annotation.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(PerfAnnotationDisabled, testPerfAnnotation) {
  PERF_OPERATION(perf);
<<<<<<< HEAD
  PERF_REPORT(perf, "alpha", "0");
  PERF_REPORT(perf, "beta", "1");
  PERF_REPORT(perf, "alpha", "2");
  PERF_REPORT(perf, "beta", "3");
=======
  PERF_RECORD(perf, "alpha", "0");
  PERF_RECORD(perf, "beta", "1");
  PERF_RECORD(perf, "alpha", "2");
  PERF_RECORD(perf, "beta", "3");
>>>>>>> perf-annotation-lib
  std::string report = PERF_TO_STRING();
  EXPECT_TRUE(report.empty());
  PERF_CLEAR();
}

} // namespace Envoy
