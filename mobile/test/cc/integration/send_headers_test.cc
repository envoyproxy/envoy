#include "test/common/http/filters/assertion/filter.pb.h"
#include "test/common/integration/engine_with_test_server.h"
#include "test/common/integration/test_server.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/engine_types.h"
#include "library/common/http/header_utility.h"

namespace Envoy {

TEST(SendHeadersTest, Success) {
  envoymobile::extensions::filters::http::assertion::Assertion assertion;
  auto* http_request_headers_match =
      assertion.mutable_match_config()->mutable_http_request_headers_match();
  auto* headers1 = http_request_headers_match->add_headers();
  headers1->set_name(":method");
  headers1->set_exact_match("GET");
  auto* headers2 = http_request_headers_match->add_headers();
  headers2->set_name(":scheme");
  headers2->set_exact_match("https");
  auto* headers3 = http_request_headers_match->add_headers();
  headers3->set_name(":path");
  headers3->set_exact_match("/");
  ProtobufWkt::Any typed_config;
  typed_config.set_type_url(
      "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion");
  std::string serialized_assertion;
  assertion.SerializeToString(&serialized_assertion);
  typed_config.set_value(serialized_assertion);

  absl::Notification engine_running;
  Platform::EngineBuilder engine_builder;
  engine_builder.enforceTrustChainVerification(false)
      .setLogLevel(Logger::Logger::debug)
      .addNativeFilter("envoy.filters.http.assertion", typed_config)
      .setOnEngineRunning([&]() { engine_running.Notify(); });
  EngineWithTestServer engine_with_test_server(engine_builder, TestServerType::HTTP2_WITH_TLS);
  engine_running.WaitForNotification();

  std::string actual_status_code;
  bool actual_end_stream;
  absl::Notification stream_complete;
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [&](const Http::ResponseHeaderMap& headers, bool end_stream,
                                     envoy_stream_intel) {
    actual_status_code = headers.getStatusValue();
    actual_end_stream = end_stream;
  };
  stream_callbacks.on_data_ = [&](const Buffer::Instance&, uint64_t /* length */, bool end_stream,
                                  envoy_stream_intel) { actual_end_stream = end_stream; };
  stream_callbacks.on_complete_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  stream_callbacks.on_error_ = [&](const EnvoyError&, envoy_stream_intel,
                                   envoy_final_stream_intel) { stream_complete.Notify(); };
  stream_callbacks.on_cancel_ = [&](envoy_stream_intel, envoy_final_stream_intel) {
    stream_complete.Notify();
  };
  auto stream_prototype = engine_with_test_server.engine()->streamClient()->newStreamPrototype();
  Platform::StreamSharedPtr stream = stream_prototype->start(std::move(stream_callbacks));

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  headers->addCopy(Http::LowerCaseString(":method"), "GET");
  headers->addCopy(Http::LowerCaseString(":scheme"), "https");
  headers->addCopy(Http::LowerCaseString(":authority"),
                   engine_with_test_server.testServer().getAddress());
  headers->addCopy(Http::LowerCaseString(":path"), "/");
  stream->sendHeaders(std::move(headers), true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(actual_status_code, "200");
  EXPECT_TRUE(actual_end_stream);
}

} // namespace Envoy
