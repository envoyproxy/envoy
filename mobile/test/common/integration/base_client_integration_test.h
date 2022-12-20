#pragma once

#include "test/integration/integration.h"

#include "library/cc/engine_builder.h"
#include "library/cc/stream.h"
#include "library/cc/stream_prototype.h"
#include "library/common/http/client.h"
#include "library/common/types/c_types.h"
#include <vector>

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
std::string defaultConfig();

// A base class for Envoy Mobile client integration tests which interact with Envoy through the
// Http::Client class.
//
// TODO(junr03): move this to derive from the ApiListenerIntegrationTest after moving that class
// into a test lib.
class BaseClientIntegrationTest : public BaseIntegrationTest {
public:
  BaseClientIntegrationTest(Network::Address::IpVersion ip_version,
                            const std::string& bootstrap_config = defaultConfig());
  virtual ~BaseClientIntegrationTest() = default;

protected:
  envoy_engine_t& rawEngine() { return engine_->engine_; }
  virtual void initialize() override;
  virtual void cleanup();
  void createEnvoy() override;
  void threadRoutine(absl::Notification& engine_running);
  // Must be called manually by subclasses in their TearDown();
  void TearDown();
  // helpers to access protected functions in the friend class
  void setOverrideConfigForTests(Platform::EngineBuilder builder, std::string config) {
    builder.setOverrideConfigForTests(config);
  }
  void setAdminAddressPathForTests(Platform::EngineBuilder& builder, std::string admin) {
    builder.setAdminAddressPathForTests(admin);
  }
  // Converts TestRequestHeaderMapImpl to Envoy::Platform::RequestHeadersSharedPtr
  Envoy::Platform::RequestHeadersSharedPtr
  envoyToMobileHeaders(const Http::TestRequestHeaderMapImpl& request_headers);
  Event::ProvisionalDispatcherPtr dispatcher_ = std::make_unique<Event::ProvisionalDispatcher>();
  envoy_http_callbacks bridge_callbacks_;
  ConditionalInitializer* terminal_callback_;
  std::vector<std::unique_ptr<ConditionalInitializer>> multi_terminal_callbacks_;
  callbacks_called* cc_;
  std::vector<std::unique_ptr<callbacks_called>> multi_cc_;
  Http::TestRequestHeaderMapImpl default_request_headers_;
  Event::DispatcherPtr full_dispatcher_;
  Platform::StreamPrototypeSharedPtr stream_prototype_;
  std::vector<Platform::StreamPrototypeSharedPtr> multi_stream_prototypes_;
  Platform::StreamSharedPtr stream_;
  std::vector<Platform::StreamSharedPtr> multi_streams_;
  Platform::EngineSharedPtr engine_;
  std::vector<Platform::EngineSharedPtr> multi_engines_;
  Thread::ThreadPtr envoy_thread_;
  bool explicit_flow_control_ = false;
  bool expect_dns_ = true;
  bool override_builder_config_ = false;
  // if set to a value > 1 in a test, multiple engines are created and stored in multi_engines_
  int num_engines_for_test_ = 1;
  // True if data plane requests are expected in the test; false otherwise.
  bool expect_data_streams_ = true;
  Platform::EngineBuilder builder_;
};

} // namespace Envoy
