#include "test/common/http/filters/assertion/filter.pb.h"
#include "test/common/integration/client_engine_with_test_server.h"
#include "test/common/integration/test_server.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/client_engine_builder.h"
#include "library/common/engine_types.h"
#include "library/common/http/header_utility.h"

#if defined(ENVOY_ENABLE_FULL_PROTOS)

namespace Envoy {

class ClientReceiveTrailersTest : public testing::TestWithParam<TestServerType> {};

INSTANTIATE_TEST_SUITE_P(ClientReceiveTrailersTest, ClientReceiveTrailersTest,
                         testing::Values(TestServerType::HTTP1_WITHOUT_TLS,
                                         TestServerType::HTTP1_WITH_TLS,
                                         TestServerType::HTTP2_WITH_TLS,
                                         TestServerType::HTTP3));

TEST_P(ClientReceiveTrailersTest, Success) {
  absl::Notification engine_running;
  Platform::ClientEngineBuilder engine_builder;
  engine_builder.enableLogger(false)
      .setLogLevel(Logger::Logger::debug)
      .setOnEngineRunning([&]() { engine_running.Notify(); });
  ClientEngineWithTestServer client_engine_with_test_server(
      engine_builder, GetParam(),
      /* headers= */ {},
      /* body= */ "", /* trailers= */ {{"foo", "bar"}});
  engine_running.WaitForNotification();

  bool on_trailers_was_called = false;
  std::string actual_status_code;
  absl::Notification stream_complete;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap& headers, bool /* end_stream */,
                                     envoy_stream_intel) {
    actual_status_code = headers.getStatusValue();
  };
  stream_callbacks.on_trailers_ = [&](const Http::ResponseTrailerMap& trailers,
                                      envoy_stream_intel) {
    on_trailers_was_called = true;
    EXPECT_EQ("bar", trailers.get(Http::LowerCaseString("foo"))[0]->value().getStringView());
  };
  stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  stream_callbacks.on_error_ = [&](const EnvoyError&, envoy_stream_intel,
                                   envoy_final_stream_intel) { stream_complete.Notify(); };
  stream_callbacks.on_cancel_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  auto stream_prototype =
      client_engine_with_test_server.engine()->streamClient()->newStreamPrototype();
  Platform::StreamSharedPtr stream = stream_prototype->start(std::move(stream_callbacks));

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  headers->addCopy(Http::LowerCaseString(":method"), "GET");
  headers->addCopy(Http::LowerCaseString(":scheme"), GetParam() == TestServerType::HTTP1_WITHOUT_TLS ? "http" : "https");
  headers->addCopy(Http::LowerCaseString(":authority"),
                   client_engine_with_test_server.testServer().getAddress());
  headers->addCopy(Http::LowerCaseString(":path"), "/");
  stream->sendHeaders(std::move(headers), true);

  stream_complete.WaitForNotification();

  EXPECT_TRUE(on_trailers_was_called);
  EXPECT_EQ(actual_status_code, "200");
}

} // namespace Envoy

#endif // defined(ENVOY_ENABLE_FULL_PROTOS)
