#include "base_client_integration_test.h"

#include <string>

#include "test/common/http/common.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/common/http/header_utility.h"
#include "library/common/internal_engine.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace {

void validateStreamIntel(const envoy_final_stream_intel& final_intel, bool expect_dns,
                         bool upstream_tls, bool is_first_request) {
  if (expect_dns) {
    EXPECT_NE(-1, final_intel.dns_start_ms);
    EXPECT_NE(-1, final_intel.dns_end_ms);
  }

  if (upstream_tls) {
    EXPECT_GT(final_intel.ssl_start_ms, 0);
    EXPECT_GT(final_intel.ssl_end_ms, 0);
  } else {
    EXPECT_EQ(-1, final_intel.ssl_start_ms);
    EXPECT_EQ(-1, final_intel.ssl_end_ms);
  }

  ASSERT_NE(-1, final_intel.stream_start_ms);
  ASSERT_NE(-1, final_intel.connect_start_ms);
  ASSERT_NE(-1, final_intel.connect_end_ms);
  ASSERT_NE(-1, final_intel.sending_start_ms);
  ASSERT_NE(-1, final_intel.sending_end_ms);
  ASSERT_NE(-1, final_intel.response_start_ms);
  ASSERT_NE(-1, final_intel.stream_end_ms);

  if (is_first_request) {
    ASSERT_LE(final_intel.stream_start_ms, final_intel.connect_start_ms);
  }
  ASSERT_LE(final_intel.connect_start_ms, final_intel.connect_end_ms);
  ASSERT_LE(final_intel.connect_end_ms, final_intel.sending_start_ms);
  ASSERT_LE(final_intel.sending_start_ms, final_intel.sending_end_ms);
  ASSERT_LE(final_intel.response_start_ms, final_intel.stream_end_ms);
}

} // namespace

// Use the Envoy mobile default config as much as possible in this test.
// There are some config modifiers below which do result in deltas.
// Note: This function is only used to build the Engine if `override_builder_config_` is true.
envoy::config::bootstrap::v3::Bootstrap defaultConfig() {
  Platform::EngineBuilder builder;
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap = builder.generateBootstrap();
  envoy::config::bootstrap::v3::Bootstrap to_return = *bootstrap;
  return to_return;
}

BaseClientIntegrationTest::BaseClientIntegrationTest(Network::Address::IpVersion ip_version)
    : BaseIntegrationTest(BaseIntegrationTest::defaultAddressFunction(ip_version), ip_version,
                          defaultConfig()) {
  skip_tag_extraction_rule_check_ = true;
  full_dispatcher_ = api_->allocateDispatcher("fake_envoy_mobile");
  use_lds_ = false;
  autonomous_upstream_ = true;
  defer_listener_finalization_ = true;
  memset(&last_stream_final_intel_, 0, sizeof(envoy_final_stream_intel));

  builder_.setLogLevel(
      static_cast<Logger::Logger::Levels>(TestEnvironment::getOptions().logLevel()));
  // The admin interface gets added by default in the ConfigHelper's constructor. Since the admin
  // interface gets compiled out by default in Envoy Mobile, remove it from the ConfigHelper's
  // bootstrap config.
  config_helper_.addConfigModifier(
      [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) { bootstrap.clear_admin(); });
}

void BaseClientIntegrationTest::initialize() {
  BaseIntegrationTest::initialize();
  {
    absl::MutexLock l(&engine_lock_);
    stream_prototype_ = engine_->streamClient()->newStreamPrototype();
  }

  HttpTestUtility::addDefaultHeaders(default_request_headers_);
  default_request_headers_.setHost(fake_upstreams_[0]->localAddress()->asStringView());
}

EnvoyStreamCallbacks BaseClientIntegrationTest::createDefaultStreamCallbacks() {
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [this](const Http::ResponseHeaderMap& headers, bool,
                                        envoy_stream_intel intel) {
    cc_.on_headers_calls_++;
    cc_.status_ = absl::StrCat(headers.getStatusValue());
    cc_.on_header_consumed_bytes_from_response_ = intel.consumed_bytes_from_response;
  };
  stream_callbacks.on_data_ = [this](const Buffer::Instance&, uint64_t /* length */,
                                     bool /* end_stream */,
                                     envoy_stream_intel) { cc_.on_data_calls_++; };
  stream_callbacks.on_complete_ = [this](envoy_stream_intel, envoy_final_stream_intel final_intel) {
    memcpy(&last_stream_final_intel_, &final_intel, sizeof(envoy_final_stream_intel));
    if (expect_data_streams_) {
      validateStreamIntel(final_intel, expect_dns_, upstream_tls_, cc_.on_complete_calls_ == 0);
    }
    cc_.on_complete_received_byte_count_ = final_intel.received_byte_count;
    cc_.on_complete_calls_++;
    cc_.terminal_callback_->setReady();
  };
  stream_callbacks.on_error_ = [this](EnvoyError, envoy_stream_intel, envoy_final_stream_intel) {
    cc_.on_error_calls_++;
    cc_.terminal_callback_->setReady();
  };
  stream_callbacks.on_cancel_ = [this](envoy_stream_intel, envoy_final_stream_intel final_intel) {
    EXPECT_NE(-1, final_intel.stream_start_ms);
    cc_.on_cancel_calls_++;
    cc_.terminal_callback_->setReady();
  };
  return stream_callbacks;
}

Platform::StreamSharedPtr
BaseClientIntegrationTest::createNewStream(EnvoyStreamCallbacks&& stream_callbacks) {
  return stream_prototype_->start(std::move(stream_callbacks), explicit_flow_control_);
}

void BaseClientIntegrationTest::threadRoutine(absl::Notification& engine_running) {
  builder_.setOnEngineRunning([&]() { engine_running.Notify(); });
  {
    absl::MutexLock l(&engine_lock_);
    engine_ = builder_.build();
  }
  full_dispatcher_->run(Event::Dispatcher::RunType::Block);
}

void BaseClientIntegrationTest::TearDown() {
  if (xds_connection_ != nullptr) {
    cleanUpXdsConnection();
  }
  test_server_.reset();
  fake_upstreams_.clear();
  {
    absl::MutexLock l(&engine_lock_);
    if (engine_) {
      engine_->terminate();
      engine_.reset();
    }
  }
  stream_.reset();
  stream_prototype_.reset();
  full_dispatcher_->exit();
  if (envoy_thread_) {
    envoy_thread_->join();
    envoy_thread_.reset();
  }
}

void BaseClientIntegrationTest::createEnvoy() {
  std::vector<uint32_t> ports;
  for (auto& upstream : fake_upstreams_) {
    if (upstream->localAddress()->ip()) {
      ports.push_back(upstream->localAddress()->ip()->port());
    }
  }

  absl::Notification engine_running;
  envoy_thread_ = api_->threadFactory().createThread(
      [this, &engine_running]() -> void { threadRoutine(engine_running); });
  engine_running.WaitForNotification();
}

uint64_t BaseClientIntegrationTest::getCounterValue(const std::string& name) {
  uint64_t counter_value = 0UL;
  uint64_t* counter_value_ptr = &counter_value;
  absl::Notification counter_value_set;
  internalEngine()->dispatcher().post([&] {
    Stats::CounterSharedPtr counter =
        TestUtility::findCounter(internalEngine()->getStatsStore(), name);
    if (counter != nullptr) {
      *counter_value_ptr = counter->value();
    }
    counter_value_set.Notify();
  });

  EXPECT_TRUE(counter_value_set.WaitForNotificationWithTimeout(absl::Seconds(5)));
  return counter_value;
}

testing::AssertionResult BaseClientIntegrationTest::waitForCounterGe(const std::string& name,
                                                                     uint64_t value) {
  constexpr std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (getCounterValue(name) < value) {
    timeSystem().advanceTimeWait(std::chrono::milliseconds(10));
    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      return testing::AssertionFailure()
             << fmt::format("timed out waiting for {} to be {}", name, value);
    }
  }
  return testing::AssertionSuccess();
}

uint64_t BaseClientIntegrationTest::getGaugeValue(const std::string& name) {
  uint64_t gauge_value = 0UL;
  uint64_t* gauge_value_ptr = &gauge_value;
  absl::Notification gauge_value_set;
  internalEngine()->dispatcher().post([&] {
    Stats::GaugeSharedPtr gauge = TestUtility::findGauge(internalEngine()->getStatsStore(), name);
    if (gauge != nullptr) {
      *gauge_value_ptr = gauge->value();
    }
    gauge_value_set.Notify();
  });
  EXPECT_TRUE(gauge_value_set.WaitForNotificationWithTimeout(absl::Seconds(5)));
  return gauge_value;
}

testing::AssertionResult BaseClientIntegrationTest::waitForGaugeGe(const std::string& name,
                                                                   uint64_t value) {
  constexpr std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (getGaugeValue(name) < value) {
    timeSystem().advanceTimeWait(std::chrono::milliseconds(10));
    if (timeout != std::chrono::milliseconds::zero() && !bound.withinBound()) {
      return testing::AssertionFailure()
             << fmt::format("timed out waiting for {} to be {}", name, value);
    }
  }
  return testing::AssertionSuccess();
}

} // namespace Envoy
