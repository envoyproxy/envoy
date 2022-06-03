#include "source/extensions/http/header_formatters/preserve_case/config.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/common/integration/base_client_integration_test.h"
#include "test/integration/autonomous_upstream.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/http/header_utility.h"

namespace Envoy {
namespace {

void validateStreamIntel(const envoy_final_stream_intel& final_intel) {
  EXPECT_NE(-1, final_intel.dns_start_ms);
  EXPECT_NE(-1, final_intel.dns_end_ms);

  // This test doesn't do TLS.
  EXPECT_EQ(-1, final_intel.ssl_start_ms);
  EXPECT_EQ(-1, final_intel.ssl_end_ms);

  ASSERT_NE(-1, final_intel.stream_start_ms);
  ASSERT_NE(-1, final_intel.connect_start_ms);
  ASSERT_NE(-1, final_intel.connect_end_ms);
  ASSERT_NE(-1, final_intel.sending_start_ms);
  ASSERT_NE(-1, final_intel.sending_end_ms);
  ASSERT_NE(-1, final_intel.response_start_ms);
  ASSERT_NE(-1, final_intel.stream_end_ms);

  ASSERT_LE(final_intel.stream_start_ms, final_intel.connect_start_ms);
  ASSERT_LE(final_intel.connect_start_ms, final_intel.connect_end_ms);
  ASSERT_LE(final_intel.connect_end_ms, final_intel.sending_start_ms);
  ASSERT_LE(final_intel.sending_start_ms, final_intel.sending_end_ms);
  ASSERT_LE(final_intel.response_start_ms, final_intel.stream_end_ms);
}

class ClientIntegrationTest : public BaseClientIntegrationTest,
                              public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ClientIntegrationTest() : BaseClientIntegrationTest(/*ip_version=*/GetParam()) {}

  void SetUp() override {
    setUpstreamCount(config_helper_.bootstrap().static_resources().clusters_size());
    // TODO(abeyad): Add paramaterized tests for HTTP1, HTTP2, and HTTP3.
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void TearDown() override {
    ASSERT_EQ(cc_.on_complete_calls + cc_.on_cancel_calls + cc_.on_error_calls, 1);
    cleanup();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClientIntegrationTest, Basic) {
  initialize();

  bridge_callbacks_.on_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                 void* context) -> void* {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "");
    } else {
      EXPECT_EQ(c_data.length, 10);
    }
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_data_calls++;
    release_envoy_data(c_data);
    return nullptr;
  };

  // Build a set of request headers.
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Build body data
  envoy_data c_data = Data::Utility::toBridgeData(request_data);

  // Build a set of request trailers.
  // TODO: update the autonomous upstream to assert on trailers, or to send trailers back.
  Http::TestRequestTrailerMapImpl trailers;
  envoy_headers c_trailers = Http::Utility::toBridgeHeaders(trailers);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, false);
    http_client_->sendHeaders(stream_, c_headers, false);
    http_client_->sendData(stream_, c_data, false);
    http_client_->sendTrailers(stream_, c_trailers);
  });
  terminal_callback_.waitReady();

  validateStreamIntel(cc_.final_intel);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 2);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  ASSERT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  ASSERT_EQ(cc_.on_complete_received_byte_count, 67);

  // stream_success gets charged for 2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_success", 1);
}

TEST_P(ClientIntegrationTest, BasicNon2xx) {
  initialize();

  // Set response header status to be non-2xx to test that the correct stats get charged.
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "503"}, {"content-length", "0"}})));

  // Build a set of request headers.
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, false);
    http_client_->sendHeaders(stream_, c_headers, true);
  });
  terminal_callback_.waitReady();

  validateStreamIntel(cc_.final_intel);
  ASSERT_EQ(cc_.on_error_calls, 0);
  ASSERT_EQ(cc_.status, "503");
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);

  // stream_failure gets charged for all non-2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_failure", 1);
}

TEST_P(ClientIntegrationTest, BasicReset) {
  initialize();

  // Cause an upstream reset after request is complete.
  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_REQUEST, "yes");
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, false);
    http_client_->sendHeaders(stream_, c_headers, true);
  });
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls, 1);
  ASSERT_EQ(cc_.on_headers_calls, 0);
  // Reset causes a charge to stream_failure.
  test_server_->waitForCounterEq("http.client.stream_failure", 1);
}

TEST_P(ClientIntegrationTest, BasicCancel) {
  autonomous_upstream_ = false;
  initialize();

  bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel,
                                    void* context) -> void* {
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_headers_calls++;
    cc_->status = response_headers->Status()->value().getStringView();
    // Lie and say the request is complete, so the test has something to wait
    // on.
    cc_->terminal_callback->setReady();
    return nullptr;
  };

  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, false);
    http_client_->sendHeaders(stream_, c_headers, true);
  });

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send an incomplete response.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 15\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));
  // For this test only, the terminal callback is called when headers arrive.
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);

  // Now cancel, and make sure the cancel is received.
  dispatcher_->post([&]() -> void { http_client_->cancelStream(stream_); });
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_cancel_calls, 1);
}

TEST_P(ClientIntegrationTest, CancelWithPartialStream) {
  autonomous_upstream_ = false;
  initialize();

  bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel,
                                    void* context) -> void* {
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_headers_calls++;
    cc_->status = response_headers->Status()->value().getStringView();
    // Lie and say the request is complete, so the test has something to wait
    // on.
    cc_->terminal_callback->setReady();
    return nullptr;
  };

  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Create a stream with explicit flow control.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, true);
    http_client_->sendHeaders(stream_, c_headers, true);
  });

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send a complete response with body.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nasd";
  ASSERT_TRUE(upstream_connection->write(response));
  // For this test only, the terminal callback is called when headers arrive.
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  // Due to explicit flow control, the upstream stream is complete, but the
  // callbacks will not be called for data and completion. Cancel the stream
  // and make sure the cancel is received.
  dispatcher_->post([&]() -> void { http_client_->cancelStream(stream_); });
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_cancel_calls, 1);
}

// TODO(junr03): test with envoy local reply with local stream not closed, which causes a reset
// fired from the Http:ConnectionManager rather than the Http::Client. This cannot be done in
// unit tests because the Http::ConnectionManager is mocked using a mock response encoder.

// Test header key case sensitivity.
TEST_P(ClientIntegrationTest, CaseSensitive) {
  Envoy::Extensions::Http::HeaderFormatters::PreserveCase::
      forceRegisterPreserveCaseFormatterFactoryConfig();
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto typed_extension_config = protocol_options.mutable_explicit_http_config()
                                      ->mutable_http_protocol_options()
                                      ->mutable_header_key_format()
                                      ->mutable_stateful_formatter();
    typed_extension_config->set_name("preserve_case");
    typed_extension_config->mutable_typed_config()->set_type_url(
        "type.googleapis.com/"
        "envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig");
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  autonomous_upstream_ = false;
  initialize();

  bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel,
                                    void* context) -> void* {
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_headers_calls++;
    cc_->status = response_headers->Status()->value().getStringView();
    EXPECT_EQ("My-ResponsE-Header",
              response_headers->formatter().value().get().format("my-response-header"));
    return nullptr;
  };

  // Build a set of request headers.
  default_request_headers_.header_map_->setFormatter(
      std::make_unique<
          Extensions::Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter>(
          false, envoy::extensions::http::header_formatters::preserve_case::v3::
                     PreserveCaseFormatterConfig::DEFAULT));
  default_request_headers_.addCopy("FoO", "bar");
  default_request_headers_.header_map_->formatter().value().get().processKey("FoO");
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, false);
    http_client_->sendHeaders(stream_, c_headers, true);
  });

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has preserved cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  EXPECT_TRUE(absl::StrContains(upstream_request, "FoO: bar")) << upstream_request;

  // Verify that the downstream response has preserved cased headers.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nMy-ResponsE-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  terminal_callback_.waitReady();

  validateStreamIntel(cc_.final_intel);
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 1);

  // stream_success gets charged for 2xx status codes.
  test_server_->waitForCounterEq("http.client.stream_success", 1);
}

TEST_P(ClientIntegrationTest, TimeoutOnRequestPath) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* em_hcm = listener->mutable_api_listener()->mutable_api_listener();
    auto hcm =
        MessageUtil::anyConvert<envoy::extensions::filters::network::http_connection_manager::v3::
                                    EnvoyMobileHttpConnectionManager>(*em_hcm);
    hcm.mutable_config()->mutable_stream_idle_timeout()->set_seconds(1);
    em_hcm->PackFrom(hcm);
  });

  autonomous_upstream_ = false;
  initialize();

  bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel,
                                    void* context) -> void* {
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_headers_calls++;
    cc_->status = response_headers->Status()->value().getStringView();
    return nullptr;
  };

  // Build a set of request headers.
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, false);
    http_client_->sendHeaders(stream_, c_headers, false);
  });

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 0);
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_error_calls, 1);
}

TEST_P(ClientIntegrationTest, TimeoutOnResponsePath) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* em_hcm = listener->mutable_api_listener()->mutable_api_listener();
    auto hcm =
        MessageUtil::anyConvert<envoy::extensions::filters::network::http_connection_manager::v3::
                                    EnvoyMobileHttpConnectionManager>(*em_hcm);
    hcm.mutable_config()->mutable_stream_idle_timeout()->set_seconds(1);
    em_hcm->PackFrom(hcm);
  });

  autonomous_upstream_ = false;
  initialize();

  bridge_callbacks_.on_headers = [](envoy_headers c_headers, bool, envoy_stream_intel,
                                    void* context) -> void* {
    Http::ResponseHeaderMapPtr response_headers = toResponseHeaders(c_headers);
    callbacks_called* cc_ = static_cast<callbacks_called*>(context);
    cc_->on_headers_calls++;
    cc_->status = response_headers->Status()->value().getStringView();
    return nullptr;
  };

  // Build a set of request headers.
  envoy_headers c_headers = Http::Utility::toBridgeHeaders(default_request_headers_);

  // Create a stream.
  dispatcher_->post([&]() -> void {
    http_client_->startStream(stream_, bridge_callbacks_, false);
    http_client_->sendHeaders(stream_, c_headers, true);
  });

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send response headers but no body.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nMy-ResponsE-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_error_calls, 1);
}

} // namespace
} // namespace Envoy
