#include "test/common/integration/engine_with_test_server.h"
#include "test/common/integration/test_server.h"

#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/cc/envoy_error.h"
#include "library/cc/request_headers_builder.h"
#include "library/cc/request_method.h"

namespace Envoy {
namespace {

TEST(SendHeadersTest, CanSendHeaders) {
  absl::Notification engine_running;
  Platform::EngineBuilder engine_builder;
  engine_builder.enforceTrustChainVerification(false)
      .setLogLevel(Logger::Logger::debug)
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

  Platform::RequestHeadersBuilder request_headers_builder(
      Platform::RequestMethod::GET, "https",
      absl::StrFormat("localhost:%d", engine_with_test_server.testServer().getPort()), "/");
  auto request_headers = request_headers_builder.build();
  auto request_headers_ptr =
      Platform::RequestHeadersSharedPtr(new Platform::RequestHeaders(request_headers));
  stream->sendHeaders(request_headers_ptr, true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, 200);
  EXPECT_TRUE(actual_end_stream);
}

} // namespace
} // namespace Envoy
