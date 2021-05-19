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

const static std::string CONFIG_TEMPLATE = "\
static_resources:\n\
  listeners:\n\
  - name: base_api_listener\n\
    address:\n\
      socket_address:\n\
        protocol: TCP\n\
        address: 0.0.0.0\n\
        port_value: 10000\n\
    api_listener:\n\
      api_listener:\n\
        \"@type\": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager\n\
        stat_prefix: hcm\n\
        route_config:\n\
          name: api_router\n\
          virtual_hosts:\n\
            - name: api\n\
              domains:\n\
                - \"*\"\n\
              routes:\n\
                - match:\n\
                    prefix: \"/\"\n\
                  direct_response:\n\
                    status: 200\n\
        http_filters:\n\
          - name: envoy.filters.http.assertion\n\
            typed_config:\n\
              \"@type\": type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion\n\
              match_config:\n\
                http_request_headers_match:\n\
                  headers:\n\
                    - name: \":authority\"\n\
                      exact_match: example.com\n\
          - name: envoy.router\n\
            typed_config:\n\
              \"@type\": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router\n\
";

struct Status {
  int status_code;
  bool end_stream;
};

void sendRequest(Platform::EngineSharedPtr engine, Status& status,
                 absl::Notification& stream_complete) {
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
  auto stream = (*stream_prototype)
                    .setOnHeaders([&](Platform::ResponseHeadersSharedPtr headers, bool end_stream) {
                      status.status_code = headers->httpStatus();
                      status.end_stream = end_stream;
                    })
                    .setOnComplete([&]() { stream_complete.Notify(); })
                    .setOnError([&](Platform::EnvoyErrorSharedPtr envoy_error) {
                      (void)envoy_error;
                      stream_complete.Notify();
                    })
                    .setOnCancel([&]() { stream_complete.Notify(); })
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
