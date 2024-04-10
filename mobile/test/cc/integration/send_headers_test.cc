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
  TestServer test_server;
  test_server.startTestServer(TestServerType::HTTP2_WITH_TLS);

  absl::Notification engine_running;
  Platform::EngineBuilder engine_builder;
  Platform::EngineSharedPtr engine = engine_builder.enforceTrustChainVerification(false)
                                         .setLogLevel(Logger::Logger::debug)
                                         .setOnEngineRunning([&]() { engine_running.Notify(); })
                                         .build();
  engine_running.WaitForNotification();

  int actual_status_code;
  bool actual_end_stream;
  absl::Notification stream_complete;
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
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
      absl::StrFormat("localhost:%d", test_server.getServerPort()), "/");
  auto request_headers = request_headers_builder.build();
  auto request_headers_ptr =
      Platform::RequestHeadersSharedPtr(new Platform::RequestHeaders(request_headers));
  stream->sendHeaders(request_headers_ptr, true);
  stream_complete.WaitForNotification();

  // It is important that the we shutdown the TestServer first before terminating the Engine. This
  // is because when the Engine is terminated, the Logger::Context will be destroyed and
  // Logger::Context is a global variable that is used by both Engine and TestServer. By shutting
  // down the TestServer first, the TestServer will no longer access a Logger::Context that has been
  // destroyed.
  test_server.shutdownTestServer();

  EXPECT_EQ(actual_status_code, 200);
  EXPECT_TRUE(actual_end_stream);
  EXPECT_EQ(engine->terminate(), ENVOY_SUCCESS);
}

} // namespace
} // namespace Envoy
