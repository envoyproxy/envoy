#pragma once

#include "test/integration/integration.h"

#include "library/cc/engine_builder.h"
#include "library/cc/stream.h"
#include "library/cc/stream_prototype.h"
#include "library/common/http/client.h"
#include "library/common/types/c_types.h"

namespace Envoy {

// Maintains statistics and status data obtained from the Http::Client callbacks.
typedef struct {
  uint32_t on_headers_calls;
  uint32_t on_data_calls;
  uint32_t on_complete_calls;
  uint32_t on_error_calls;
  uint32_t on_cancel_calls;
  uint64_t on_header_consumed_bytes_from_response;
  uint64_t on_complete_received_byte_count;
  std::string status;
  ConditionalInitializer* terminal_callback;
  envoy_final_stream_intel final_intel;
} callbacks_called;

// Based on Http::Utility::toRequestHeaders() but only used for these tests.
Http::ResponseHeaderMapPtr toResponseHeaders(envoy_headers headers);

// Creates a default bootstrap config from the EngineBuilder.
// Only used to build the Engine if `override_builder_config_` is set to true.
envoy::config::bootstrap::v3::Bootstrap defaultConfig();

// A base class for Envoy Mobile client integration tests which interact with Envoy through the
// Http::Client class.
//
// TODO(junr03): move this to derive from the ApiListenerIntegrationTest after moving that class
// into a test lib.
class BaseClientIntegrationTest : public BaseIntegrationTest {
public:
  BaseClientIntegrationTest(Network::Address::IpVersion ip_version);
  virtual ~BaseClientIntegrationTest() = default;
  // Note: This class does not inherit from testing::Test and so this TearDown() method
  // does not override testing::Test::TearDown(). As a result, it will not be called
  // automatically by gtest during shutdown and must be called manually.
  void TearDown();

protected:
  envoy_engine_t& rawEngine() {
    absl::MutexLock l(&engine_lock_);
    return engine_->engine_;
  }
  virtual void initialize() override;
  void createEnvoy() override;
  void threadRoutine(absl::Notification& engine_running);

  // Converts TestRequestHeaderMapImpl to Envoy::Platform::RequestHeadersSharedPtr
  Envoy::Platform::RequestHeadersSharedPtr
  envoyToMobileHeaders(const Http::TestRequestHeaderMapImpl& request_headers);

  // Get the value of a Counter in the Envoy instance.
  uint64_t getCounterValue(const std::string& name);
  // Wait until the Counter specified by `name` is >= `value`.
  ABSL_MUST_USE_RESULT testing::AssertionResult waitForCounterGe(const std::string& name,
                                                                 uint64_t value);
  uint64_t getGaugeValue(const std::string& name);
  ABSL_MUST_USE_RESULT testing::AssertionResult waitForGaugeGe(const std::string& name,
                                                               uint64_t value);

  Event::ProvisionalDispatcherPtr dispatcher_ = std::make_unique<Event::ProvisionalDispatcher>();
  envoy_http_callbacks bridge_callbacks_;
  ConditionalInitializer terminal_callback_;
  callbacks_called cc_{0, 0, 0, 0, 0, 0, 0, "", &terminal_callback_, {}};
  Http::TestRequestHeaderMapImpl default_request_headers_;
  Event::DispatcherPtr full_dispatcher_;
  Platform::StreamPrototypeSharedPtr stream_prototype_;
  Platform::StreamSharedPtr stream_;
  absl::Mutex engine_lock_;
  Platform::EngineSharedPtr engine_ ABSL_GUARDED_BY(engine_lock_);
  Thread::ThreadPtr envoy_thread_;
  bool explicit_flow_control_ = false;
  bool expect_dns_ = true;
  // True if data plane requests are expected in the test; false otherwise.
  bool expect_data_streams_ = true;
  Platform::EngineBuilder builder_;
  envoy_final_stream_intel last_stream_final_intel_;
};

} // namespace Envoy
