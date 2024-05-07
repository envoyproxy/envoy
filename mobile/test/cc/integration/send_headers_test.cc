#include "test/common/integration/engine_with_test_server.h"
#include "test/common/integration/test_server.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/cc/envoy_error.h"
#include "library/common/http/header_utility.h"

namespace Envoy {

inline constexpr absl::string_view ASSERTION_FILTER_TEXT_PROTO = R"(
  [type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion] {
    match_config: {
      http_request_headers_match: {
        headers: { name: ':method', exact_match: 'GET' }
        headers: { name: ':scheme', exact_match: 'https' }
        headers: { name: ':path', exact_match: '/' }
      }
    }
  }
)";

TEST(SendHeadersTest, Success) {
  absl::Notification engine_running;
  Platform::EngineBuilder engine_builder;
  engine_builder.enforceTrustChainVerification(false)
      .setLogLevel(Logger::Logger::debug)
#ifdef ENVOY_ENABLE_FULL_PROTOS
      .addNativeFilter("envoy.filters.http.assertion", std::string(ASSERTION_FILTER_TEXT_PROTO))
#endif
      .setOnEngineRunning([&]() { engine_running.Notify(); });
  EngineWithTestServer engine_with_test_server(engine_builder, TestServerType::HTTP2_WITH_TLS);
  engine_running.WaitForNotification();

  int actual_status_code;
  bool actual_end_stream;
  absl::Notification stream_complete;
  auto stream_prototype = engine_with_test_server.engine()->streamClient()->newStreamPrototype();
  Platform::StreamSharedPtr stream =
      (*stream_prototype)
          .setOnHeaders(
              [&](Platform::ResponseHeadersSharedPtr headers, bool end_stream, envoy_stream_intel) {
                actual_status_code = headers->httpStatus();
                actual_end_stream = end_stream;
              })
          .setOnData([&](envoy_data data, bool end_stream) {
            actual_end_stream = end_stream;
            data.release(data.context);
          })
          .setOnComplete(
              [&](envoy_stream_intel, envoy_final_stream_intel) { stream_complete.Notify(); })
          .setOnError([&](Platform::EnvoyErrorSharedPtr, envoy_stream_intel,
                          envoy_final_stream_intel) { stream_complete.Notify(); })
          .setOnCancel(
              [&](envoy_stream_intel, envoy_final_stream_intel) { stream_complete.Notify(); })
          .start();

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  headers->addCopy(Http::LowerCaseString(":method"), "GET");
  headers->addCopy(Http::LowerCaseString(":scheme"), "https");
  headers->addCopy(Http::LowerCaseString(":authority"),
                   engine_with_test_server.testServer().getAddress());
  headers->addCopy(Http::LowerCaseString(":path"), "/");
  stream->sendHeaders(std::move(headers), true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, 200);
  EXPECT_TRUE(actual_end_stream);
}

} // namespace Envoy
