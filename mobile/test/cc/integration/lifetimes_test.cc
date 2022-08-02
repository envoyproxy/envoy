#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine.h"
#include "library/cc/engine_builder.h"
#include "library/cc/envoy_error.h"
#include "library/cc/log_level.h"
#include "library/cc/request_headers_builder.h"
#include "library/cc/request_method.h"
#include "library/cc/response_headers.h"

namespace Envoy {
namespace {

const static std::string CONFIG_TEMPLATE = R"(
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10000
    api_listener:
      api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
              - name: api
                domains:
                  - "*"
                routes:
                  - match:
                      prefix: "/"
                    direct_response:
                      status: 200
          http_filters:
            - name: envoy.filters.http.assertion
              typed_config:
                "@type": type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion
                match_config:
                  http_request_headers_match:
                    headers:
                      - name: ":authority"
                        exact_match: example.com
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)";

struct Status {
  int status_code;
  bool end_stream;
};

void sendRequest(Platform::EngineSharedPtr engine, Status& status,
                 absl::Notification& stream_complete) {
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
  auto stream = (*stream_prototype)
                    .setOnHeaders([&](Platform::ResponseHeadersSharedPtr headers, bool end_stream,
                                      envoy_stream_intel) {
                      status.status_code = headers->httpStatus();
                      status.end_stream = end_stream;
                    })
                    .setOnComplete([&](envoy_stream_intel, envoy_final_stream_intel) {
                      stream_complete.Notify();
                    })
                    .setOnError([&](Platform::EnvoyErrorSharedPtr envoy_error, envoy_stream_intel,
                                    envoy_final_stream_intel) {
                      (void)envoy_error;
                      stream_complete.Notify();
                    })
                    .setOnCancel([&](envoy_stream_intel, envoy_final_stream_intel) {
                      stream_complete.Notify();
                    })
                    .start();

  auto request_headers =
      Platform::RequestHeadersBuilder(Platform::RequestMethod::GET, "https", "example.com", "/")
          .build();
  stream->sendHeaders(std::make_shared<Platform::RequestHeaders>(request_headers), true);
}

void sendRequestEndToEnd() {
  Platform::EngineSharedPtr engine;
  absl::Notification engine_running;
  auto engine_builder = Platform::EngineBuilder(CONFIG_TEMPLATE);
  engine = engine_builder.addLogLevel(Platform::LogLevel::debug)
               .setOnEngineRunning([&]() { engine_running.Notify(); })
               .build();
  engine_running.WaitForNotification();

  Status status;
  absl::Notification stream_complete;
  sendRequest(engine, status, stream_complete);
  stream_complete.WaitForNotification();

  EXPECT_EQ(status.status_code, 200);
  EXPECT_EQ(status.end_stream, true);

  engine->terminate();
}

// this test attempts to elicit race conditions deriving from
// the semantics of ownership on StreamCallbacks
TEST(TestLifetimes, CallbacksStayAlive) {
  for (size_t i = 0; i < 10; i++) {
    sendRequestEndToEnd();
  }
}

} // namespace
} // namespace Envoy
