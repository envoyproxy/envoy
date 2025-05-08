#include "source/extensions/outlier_detection_monitors/consecutive_errors/consecutive_errors_monitor.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

TEST(ConsecutiveErrorsMonitorTest, BasicTest) {
  // Create Consecutive Error monitor which "tripps" after 3 errors.
  const envoy::extensions::outlier_detection_monitors::common::v3::MonitorCommonSettings
      common_settings;
  ConsecutiveErrorsMonitor monitor(
      std::make_shared<ExtMonitorConfig>("test-monitor", common_settings), 3);

  // At the start, the error counter is zero.
  // Report 3 errors. Reporting the 3rd error should indicate
  // that monitor has been tripped.
  ASSERT_FALSE(monitor.onMatch());
  ASSERT_FALSE(monitor.onMatch());
  ASSERT_TRUE(monitor.onMatch());

  // Reset the monitor.
  monitor.onReset();

  ASSERT_FALSE(monitor.onMatch());
  ASSERT_FALSE(monitor.onMatch());
  // Calling onSuccess should reset the errors counter.
  monitor.onSuccess();
  ASSERT_FALSE(monitor.onMatch());
  ASSERT_FALSE(monitor.onMatch());
  ASSERT_TRUE(monitor.onMatch());
}

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
