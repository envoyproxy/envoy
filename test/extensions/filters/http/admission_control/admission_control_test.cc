#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/admission_control/admission_control.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::Ge;
using testing::Le;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

class MockThreadLocalController : public ThreadLocalController {
public:
  MOCK_METHOD0(requestTotalCount, uint32_t());
  MOCK_METHOD0(requestSuccessCount, uint32_t());
};

class AdmissionControlConfigTest : public testing::Test {
public:
  AdmissionControlConfigTest() = default;

  std::shared_ptr<AdmissionControlFilterConfig> makeConfig(const std::string& yaml) {
    AdmissionControlFilterConfig::AdmissionControlProto proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    return std::make_shared<AdmissionControlFilterConfig>(proto, runtime_, time_source_, random_, scope_, context.threadLocal());
  }

protected:
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl scope_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

class AdmissionControlTest : public testing::Test {
public:
  AdmissionControlTest() = default;

  AdmissionControlFilterConfig makeConfig(const std::string& yaml) {
    AdmissionControlFilterConfig::AdmissionControlProto proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    return AdmissionControlFilterConfig(proto, runtime_, time_source_, random_, scope_, context.threadLocal());
  }

  void generateFilter(std::string yaml) {
    auto config = makeConfig(yaml);
  }

protected:
  std::string stats_prefix_{""};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl scope_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::shared_ptr<AdmissionControlFilter> filter_;
};

class ThreadLocalControllerTest : public testing::Test {
public:
  ThreadLocalControllerTest() : window_(5), tlc_(time_system_, window_) {}

protected:
  // Submit a single request per entry in the historical data (this comes out to a single request
  // each second). The final sample does not advance time to allow for testing of this transition.
  void fillHistorySlots(const bool successes = true) {
    std::function<void()> record;
    if (successes) {
      record = [this]() { tlc_.recordSuccess(); };
    } else {
      record = [this]() { tlc_.recordFailure(); };
    }
    for (int tick = 0; tick < window_.count(); ++tick) {
      record();
      time_system_.sleep(std::chrono::seconds(1));
    }
    // Don't sleep after the final sample to allow for measurements.
    record();
  }

  Event::SimulatedTimeSystem time_system_;
  std::chrono::seconds window_;
  ThreadLocalController tlc_;
};

// Test the basic functionality of the admission controller.
TEST_F(ThreadLocalControllerTest, BasicRecord) {
  EXPECT_EQ(0, tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());

  tlc_.recordFailure();
  EXPECT_EQ(1, tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());

  tlc_.recordSuccess();
  EXPECT_EQ(2, tlc_.requestTotalCount());
  EXPECT_EQ(1, tlc_.requestSuccessCount());
}

// Verify that stale historical samples are removed when they grow stale.
TEST_F(ThreadLocalControllerTest, RemoveStaleSamples) {
  fillHistorySlots();

  // We expect a single request counted in each second of the window.
  EXPECT_EQ(window_.count(), tlc_.requestTotalCount());
  EXPECT_EQ(window_.count(), tlc_.requestSuccessCount());

  time_system_.sleep(std::chrono::seconds(1));

  // Continuing to sample requests at 1 per second should maintain the same request counts. We'll
  // record failures here.
  fillHistorySlots(false);
  EXPECT_EQ(window_.count(), tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());

  // Expect the oldest entry to go stale.
  time_system_.sleep(std::chrono::seconds(1));
  EXPECT_EQ(window_.count() - 1, tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());
}

// Verify that stale historical samples are removed when they grow stale.
TEST_F(ThreadLocalControllerTest, RemoveStaleSamples2) {
  fillHistorySlots();

  // We expect a single request counted in each second of the window.
  EXPECT_EQ(window_.count(), tlc_.requestTotalCount());
  EXPECT_EQ(window_.count(), tlc_.requestSuccessCount());

  // Let's just sit here for a full day. We expect all samples to become stale.
  time_system_.sleep(std::chrono::hours(24));

  EXPECT_EQ(0, tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());
}

// Verify that historical samples are made only when there is data to record.
TEST_F(ThreadLocalControllerTest, VerifyMemoryUsage) {
  // Make sure we don't add any null data to the history if there are sparse requests.
  tlc_.recordSuccess();
  time_system_.sleep(std::chrono::seconds(1));
  tlc_.recordSuccess();
  time_system_.sleep(std::chrono::seconds(1));
  time_system_.sleep(std::chrono::seconds(1));
  time_system_.sleep(std::chrono::seconds(1));
  tlc_.recordSuccess();
  EXPECT_EQ(3, tlc_.requestTotalCount());
  EXPECT_EQ(3, tlc_.requestSuccessCount());
}

// Verify the configuration when all fields are set.
TEST_F(AdmissionControlConfigTest, BasicTestAllConfigured) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
aggression_coefficient:
  default_value: 4.2
  runtime_key: "foo.aggression"
)EOF";


  auto config = makeConfig(yaml);

  EXPECT_FALSE(config->filterEnabled());
  EXPECT_EQ(std::chrono::seconds(1337), config->samplingWindow());
  EXPECT_EQ(4.2, config->aggression());
}

// Verify the config defaults when not specified.
TEST_F(AdmissionControlConfigTest, BasicTestMinimumConfigured) {
  // Empty config. No fields are required.
  AdmissionControlFilterConfig::AdmissionControlProto proto;

  auto config = makeConfig(yaml);

  EXPECT_TRUE(config->filterEnabled());
  EXPECT_EQ(std::chrono::seconds(120), config->samplingWindow());
  EXPECT_EQ(2.0, config->aggression());
}

// Ensure runtime fields are honored.
TEST_F(AdmissionControlConfigTest, VerifyRuntime) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
aggression_coefficient:
  default_value: 4.2
  runtime_key: "foo.aggression"
)EOF";

  auto config = makeConfig(yaml);

  EXPECT_CALL(context_.runtime_loader_.snapshot_, getBoolean("foo.enabled", false))
      .WillOnce(Return(true));
  EXPECT_TRUE(config->filterEnabled());
  EXPECT_CALL(context_.runtime_loader_.snapshot_, getDouble("foo.aggression", 4.2))
      .WillOnce(Return(1.3));
  EXPECT_EQ(1.3, config->aggression());
}

TEST_F(AdmissionControlTest, FilterDisabled) {
#if 0
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression_coefficient:
  default_value: 1.0
  runtime_key: "foo.aggression"
)EOF";
  
  auto config = makeConfig(yaml);

  // Fail lots of requests so that we'd expect a ~100% rejection rate.
  for (int i = 0; i < 1000; ++i) {
    config_->getController().recordFailure();
  }

  // We expect no rejection.
  Http::TestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
#endif
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
