#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

using namespace testing;

// Test monitor's logic of matching error types and calling appropriate methods.
class MockMonitor : public ExtMonitorBase {
public:
  MockMonitor(ExtMonitorConfigSharedPtr config) : ExtMonitorBase(config) {}
  MOCK_METHOD(bool, onError, ());
  MOCK_METHOD(void, onSuccess, ());
  MOCK_METHOD(void, onReset, ());
};

class MonitorTest : public testing::Test {
protected:
  void SetUp() override {
    envoy::extensions::outlier_detection_monitors::common::v3::MonitorCommonSettings proto_config;
    proto_config.mutable_enforcing()->set_value(43);
    config_ = std::make_shared<ExtMonitorConfig>(std::string(monitor_name_), proto_config);
    monitor_ = std::make_unique<MockMonitor>(config_);
  }

  static constexpr absl::string_view monitor_name_ = "mock-monitor";
  // Pick a easy to recognize number for enforcing.
  static constexpr uint32_t enforcing_ = 43;
  ExtMonitorConfigSharedPtr config_;
  std::unique_ptr<MockMonitor> monitor_;
};

TEST_F(MonitorTest, Config) { ASSERT_THAT(43, monitor_->enforce()); }

TEST_F(MonitorTest, ReportErrorNotTripped) {
  bool callback_called = false;
  monitor_->setExtMonitorCallback(
      [&callback_called](const ExtMonitor*) { callback_called = true; });
  EXPECT_CALL(*monitor_, onError).WillOnce(Return(false));

  monitor_->reportResult(true);

  // Callback has not been called, because onError returned false,
  // meaning that monitor has not tripped yet.
  ASSERT_FALSE(callback_called);
}

TEST_F(MonitorTest, ReportErrorTripped) {
  bool callback_called = false;
  monitor_->setExtMonitorCallback(
      [&callback_called](const ExtMonitor*) { callback_called = true; });
  EXPECT_CALL(*monitor_, onError).WillOnce(Return(true));
  // After tripping, the monitor is reset
  EXPECT_CALL(*monitor_, onReset);

  monitor_->reportResult(true);

  // Callback has been called, because onError returned true,
  // meaning that monitor has tripped.
  ASSERT_TRUE(callback_called);
}

TEST_F(MonitorTest, ReportNonError) {
  bool callback_called = false;
  monitor_->setExtMonitorCallback(
      [&callback_called](const ExtMonitor*) { callback_called = true; });
  EXPECT_CALL(*monitor_, onSuccess);

  monitor_->reportResult(false);

  ASSERT_FALSE(callback_called);
}

TEST_F(MonitorTest, Reset) {
  EXPECT_CALL(*monitor_, onReset);
  monitor_->reset();
}

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
