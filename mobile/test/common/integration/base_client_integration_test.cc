#include "base_client_integration_test.h"

#include <string>

#include "test/common/http/common.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/bridge_utility.h"
#include "library/cc/log_level.h"
#include "library/common/engine.h"
#include "library/common/http/header_utility.h"
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

// Gets the spdlog level from the test options and converts it to the Platform::LogLevel used by
// the Envoy Mobile engine.
Platform::LogLevel getPlatformLogLevelFromOptions() {
  switch (TestEnvironment::getOptions().logLevel()) {
  case spdlog::level::level_enum::trace:
    return Platform::LogLevel::trace;
  case spdlog::level::level_enum::debug:
    return Platform::LogLevel::debug;
  case spdlog::level::level_enum::info:
    return Platform::LogLevel::info;
  case spdlog::level::level_enum::warn:
    return Platform::LogLevel::warn;
  case spdlog::level::level_enum::err:
    return Platform::LogLevel::error;
  case spdlog::level::level_enum::critical:
    return Platform::LogLevel::critical;
  case spdlog::level::level_enum::off:
    return Platform::LogLevel::off;
  default:
    ENVOY_LOG_MISC(warn, "Couldn't map spdlog level {}. Using `info` level.",
                   TestEnvironment::getOptions().logLevel());
    return Platform::LogLevel::info;
  }
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

  builder_.addLogLevel(getPlatformLogLevelFromOptions());
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

  stream_prototype_->setOnHeaders(
      [this](Platform::ResponseHeadersSharedPtr headers, bool, envoy_stream_intel intel) {
        cc_.on_headers_calls++;
        cc_.status = absl::StrCat(headers->httpStatus());
        cc_.on_header_consumed_bytes_from_response = intel.consumed_bytes_from_response;
      });
  stream_prototype_->setOnData([this](envoy_data c_data, bool) {
    cc_.on_data_calls++;
    release_envoy_data(c_data);
  });
  stream_prototype_->setOnComplete(
      [this](envoy_stream_intel, envoy_final_stream_intel final_intel) {
        memcpy(&last_stream_final_intel_, &final_intel, sizeof(envoy_final_stream_intel));
        if (expect_data_streams_) {
          validateStreamIntel(final_intel, expect_dns_, upstream_tls_, cc_.on_complete_calls == 0);
        }
        cc_.on_complete_received_byte_count = final_intel.received_byte_count;
        cc_.on_complete_calls++;
        cc_.terminal_callback->setReady();
      });
  stream_prototype_->setOnError(
      [this](Platform::EnvoyErrorSharedPtr, envoy_stream_intel, envoy_final_stream_intel) {
        cc_.on_error_calls++;
        cc_.terminal_callback->setReady();
      });
  stream_prototype_->setOnCancel([this](envoy_stream_intel, envoy_final_stream_intel final_intel) {
    EXPECT_NE(-1, final_intel.stream_start_ms);
    cc_.on_cancel_calls++;
    cc_.terminal_callback->setReady();
  });

  stream_ = (*stream_prototype_).start(explicit_flow_control_);
  HttpTestUtility::addDefaultHeaders(default_request_headers_);
  default_request_headers_.setHost(fake_upstreams_[0]->localAddress()->asStringView());
}

std::shared_ptr<Platform::RequestHeaders> BaseClientIntegrationTest::envoyToMobileHeaders(
    const Http::TestRequestHeaderMapImpl& request_headers) {

  Platform::RequestHeadersBuilder builder(
      Platform::RequestMethod::GET,
      std::string(default_request_headers_.Scheme()->value().getStringView()),
      std::string(default_request_headers_.Host()->value().getStringView()),
      std::string(default_request_headers_.Path()->value().getStringView()));

  request_headers.iterate(
      [&request_headers, &builder](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
        std::string key = std::string(header.key().getStringView());
        if (request_headers.formatter().has_value()) {
          const Envoy::Http::StatefulHeaderKeyFormatter& formatter =
              request_headers.formatter().value();
          key = formatter.format(key);
        }
        auto value = std::vector<std::string>();
        value.push_back(std::string(header.value().getStringView()));
        builder.set(key, value);
        return Http::HeaderMap::Iterate::Continue;
      });

  return std::make_shared<Platform::RequestHeaders>(builder.build());
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
  auto engine = reinterpret_cast<Envoy::Engine*>(rawEngine());
  engine->dispatcher().post([&] {
    Stats::CounterSharedPtr counter = TestUtility::findCounter(engine->getStatsStore(), name);
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
  auto engine = reinterpret_cast<Envoy::Engine*>(rawEngine());
  engine->dispatcher().post([&] {
    Stats::GaugeSharedPtr gauge = TestUtility::findGauge(engine->getStatsStore(), name);
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
