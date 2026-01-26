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

class ClientSendTrailersTest : public testing::TestWithParam<TestServerType> {};

INSTANTIATE_TEST_SUITE_P(ClientSendTrailersTest, ClientSendTrailersTest,
                         testing::Values(TestServerType::HTTP1_WITHOUT_TLS,
                                         TestServerType::HTTP1_WITH_TLS,
                                         TestServerType::HTTP2_WITH_TLS, TestServerType::HTTP3));

TEST_P(ClientSendTrailersTest, Success) {
  envoymobile::extensions::filters::http::assertion::Assertion assertion;
  auto* http_request_trailers_match =
      assertion.mutable_match_config()->mutable_http_request_trailers_match();
  auto trailer = http_request_trailers_match->add_headers();
  trailer->set_name("trailer-key");
  trailer->set_exact_match("trailer-value");

  absl::Notification engine_running;
  Platform::ClientEngineBuilder engine_builder;
  envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter assertion_filter;
  assertion_filter.set_name("envoy.filters.http.assertion");
  assertion_filter.mutable_typed_config()->PackFrom(assertion);
  engine_builder.addHcmHttpFilter(std::move(assertion_filter));

  engine_builder.enableLogger(false)
      .setLogLevel(Logger::Logger::debug)
      .setOnEngineRunning([&]() { engine_running.Notify(); });
  ClientEngineWithTestServer client_engine_with_test_server(
      engine_builder, GetParam(),
      /* headers= */ {}, /* body= */ "body",
      /* trailers= */ {{"trailer-key", "trailer-value"}});
  engine_running.WaitForNotification();

  std::string actual_status_code;
  std::string actual_trailer_value;
  absl::Notification stream_complete;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap& headers, bool /* end_stream */,
                                     envoy_stream_intel) {
    actual_status_code = headers.getStatusValue();
  };
  stream_callbacks.on_trailers_ = [&](const Http::ResponseTrailerMap& trailers,
                                      envoy_stream_intel) {
    actual_trailer_value =
        trailers.get(Http::LowerCaseString("trailer-key"))[0]->value().getStringView();
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
  headers->addCopy(Http::LowerCaseString(":scheme"),
                   GetParam() == TestServerType::HTTP1_WITHOUT_TLS ? "http" : "https");
  headers->addCopy(Http::LowerCaseString(":authority"),
                   client_engine_with_test_server.testServer().getAddress());
  headers->addCopy(Http::LowerCaseString(":path"), "/");
  stream->sendHeaders(std::move(headers), false);

  auto trailers = Http::Utility::createRequestTrailerMapPtr();
  trailers->addCopy(Http::LowerCaseString("trailer-key"), "trailer-value");
  stream->close(std::move(trailers));

  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, "200");
  EXPECT_EQ(actual_trailer_value, "trailer-value");
}

} // namespace Envoy

#endif // defined(ENVOY_ENABLE_FULL_PROTOS)
