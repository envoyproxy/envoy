#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/common/integration/base_client_integration_test.h"
#include "test/common/mocks/common/mocks.h"
#include "test/integration/autonomous_upstream.h"

#include "library/common/data/utility.h"
#include "library/common/main_interface.h"
#include "library/common/network/proxy_settings.h"
#include "library/common/types/c_types.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace {

class ClientIntegrationTest : public BaseClientIntegrationTest,
                              public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ClientIntegrationTest() : BaseClientIntegrationTest(/*ip_version=*/GetParam()) {}

  void SetUp() override {
    setUpstreamCount(config_helper_.bootstrap().static_resources().clusters_size());
    // TODO(abeyad): Add paramaterized tests for HTTP1, HTTP2, and HTTP3.
    helper_handle_ = test::SystemHelperPeer::replaceSystemHelper();
    EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_))
        .WillRepeatedly(Return(true));
  }

  void createEnvoy() override {
    BaseClientIntegrationTest::createEnvoy();
    // Allow last minute addition of QUIC hints. This is done lazily as it must be done after
    // upstreams are created.
    if (add_quic_hints_) {
      auto address = fake_upstreams_[0]->localAddress();
      builder_.addQuicHint(address->ip()->addressAsString(), address->ip()->port());
    }
  }

  void TearDown() override { BaseClientIntegrationTest::TearDown(); }

  void basicTest();
  void trickleTest();

protected:
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
  bool add_quic_hints_ = false;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void ClientIntegrationTest::basicTest() {
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  stream_prototype_->setOnData([this](envoy_data c_data, bool end_stream) {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "");
    }
    cc_.on_data_calls++;
    release_envoy_data(c_data);
  });

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), false);

  envoy_data c_data = Data::Utility::toBridgeData(request_data);
  stream_->sendData(c_data);

  Platform::RequestTrailersBuilder builder;
  std::shared_ptr<Platform::RequestTrailers> trailers =
      std::make_shared<Platform::RequestTrailers>(builder.build());
  stream_->close(trailers);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 2);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  }
}

TEST_P(ClientIntegrationTest, Basic) {
  initialize();
  basicTest();
  ASSERT_EQ(cc_.on_complete_received_byte_count, 67);
  // HTTP/1
  ASSERT_EQ(1, last_stream_final_intel_.upstream_protocol);
}

TEST_P(ClientIntegrationTest, LargeResponse) {
  initialize();
  std::string data(1024 * 32, 'a');
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->setResponseBody(data);
  basicTest();
  ASSERT_EQ(cc_.on_complete_received_byte_count, 32828);
}

void ClientIntegrationTest::trickleTest() {
  autonomous_upstream_ = false;

  initialize();

  stream_prototype_->setOnData([this](envoy_data c_data, bool) {
    if (explicit_flow_control_) {
      // Allow reading up to 100 bytes.
      stream_->readData(100);
    }
    cc_.on_data_calls++;
    release_envoy_data(c_data);
  });
  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), false);
  if (explicit_flow_control_) {
    // Allow reading up to 100 bytes
    stream_->readData(100);
  }
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  envoy_data c_data = Data::Utility::toBridgeData(request_data);
  stream_->sendData(c_data);
  Platform::RequestTrailersBuilder builder;
  std::shared_ptr<Platform::RequestTrailers> trailers =
      std::make_shared<Platform::RequestTrailers>(builder.build());
  stream_->close(trailers);

  FakeHttpConnectionPtr upstream_connection;
  FakeStreamPtr upstream_request;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection));
  ASSERT_TRUE(
      upstream_connection->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request));

  upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  for (int i = 0; i < 10; ++i) {
    upstream_request->encodeData(1, i == 9);
  }

  terminal_callback_.waitReady();
}

TEST_P(ClientIntegrationTest, TrickleNoMinDelivery) {
  min_delivery_size_ = 0;
  trickleTest();
  ASSERT_LE(cc_.on_data_calls, 11);
}

TEST_P(ClientIntegrationTest, TrickleNoNoMinDeliveryExplicitFlowControl) {
  min_delivery_size_ = 0;
  explicit_flow_control_ = true;
  trickleTest();
  ASSERT_LE(cc_.on_data_calls, 11);
}

TEST_P(ClientIntegrationTest, TrickleMinDelivery) {
  trickleTest();
  ASSERT_EQ(cc_.on_data_calls, 2);
}

TEST_P(ClientIntegrationTest, TrickleNoMinDeliveryExplicitFlowControl) {
  explicit_flow_control_ = true;
  trickleTest();
  ASSERT_EQ(cc_.on_data_calls, 2);
}

TEST_P(ClientIntegrationTest, ClearTextNotPermitted) {
  EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_)).WillRepeatedly(Return(false));

  expect_data_streams_ = false;
  initialize();

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  stream_prototype_->setOnData([this](envoy_data c_data, bool end_stream) {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "Cleartext is not permitted");
    }
    cc_.on_data_calls++;
    release_envoy_data(c_data);
  });

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "400");
  ASSERT_EQ(cc_.on_data_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_P(ClientIntegrationTest, BasicHttp2) {
  EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_)).Times(0);
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());

  setUpstreamProtocol(Http::CodecType::HTTP2);
  builder_.enablePlatformCertificatesValidation(true);

  upstream_tls_ = true;

  initialize();

  default_request_headers_.setScheme("https");

  basicTest();
  // HTTP/2
  ASSERT_EQ(2, last_stream_final_intel_.upstream_protocol);
}

// Do HTTP/3 without doing the alt-svc-over-HTTP/2 dance.
TEST_P(ClientIntegrationTest, DISABLED_Http3WithQuicHints) {
  EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_)).Times(0);
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());

  setUpstreamProtocol(Http::CodecType::HTTP3);
  builder_.enablePlatformCertificatesValidation(true);
  upstream_tls_ = true;
  add_quic_hints_ = true;

  initialize();
  default_request_headers_.setScheme("https");
  basicTest();
  // HTTP/3
  ASSERT_EQ(3, last_stream_final_intel_.upstream_protocol);
}

TEST_P(ClientIntegrationTest, BasicHttps) {
  EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_)).Times(0);
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());

  builder_.enablePlatformCertificatesValidation(true);

  upstream_tls_ = true;

  initialize();
  default_request_headers_.setScheme("https");

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  stream_prototype_->setOnData([this](envoy_data c_data, bool end_stream) {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "");
    } else {
      EXPECT_EQ(c_data.length, 10);
    }
    cc_.on_data_calls++;
    release_envoy_data(c_data);
  });

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), false);

  envoy_data c_data = Data::Utility::toBridgeData(request_data);
  stream_->sendData(c_data);

  Platform::RequestTrailersBuilder builder;
  std::shared_ptr<Platform::RequestTrailers> trailers =
      std::make_shared<Platform::RequestTrailers>(builder.build());
  stream_->close(trailers);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 2);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  ASSERT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  ASSERT_EQ(cc_.on_complete_received_byte_count, 67);
}

TEST_P(ClientIntegrationTest, BasicNon2xx) {
  initialize();

  // Set response header status to be non-2xx to test that the correct stats get charged.
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "503"}, {"content-length", "0"}})));

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls, 0);
  ASSERT_EQ(cc_.status, "503");
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_P(ClientIntegrationTest, BasicReset) {
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_REQUEST, "yes");

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls, 1);
  ASSERT_EQ(cc_.on_headers_calls, 0);
}

TEST_P(ClientIntegrationTest, BasicCancel) {
  autonomous_upstream_ = false;
  initialize();
  ConditionalInitializer headers_callback;

  stream_prototype_->setOnHeaders(
      [this, &headers_callback](Platform::ResponseHeadersSharedPtr headers, bool,
                                envoy_stream_intel) {
        cc_.status = absl::StrCat(headers->httpStatus());
        cc_.on_headers_calls++;
        headers_callback.setReady();
        return nullptr;
      });

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send an incomplete response.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 15\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  headers_callback.waitReady();
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);

  // Now cancel, and make sure the cancel is received.
  stream_->cancel();
  memset(&cc_.final_intel, 0, sizeof(cc_.final_intel));
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_cancel_calls, 1);
}

TEST_P(ClientIntegrationTest, CancelWithPartialStream) {
  autonomous_upstream_ = false;
  explicit_flow_control_ = true;
  initialize();
  ConditionalInitializer headers_callback;

  stream_prototype_->setOnHeaders(
      [this, &headers_callback](Platform::ResponseHeadersSharedPtr headers, bool,
                                envoy_stream_intel) {
        cc_.status = absl::StrCat(headers->httpStatus());
        cc_.on_headers_calls++;
        headers_callback.setReady();
        return nullptr;
      });

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send a complete response with body.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nasd";
  ASSERT_TRUE(upstream_connection->write(response));
  headers_callback.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  // Due to explicit flow control, the upstream stream is complete, but the
  // callbacks will not be called for data and completion. Cancel the stream
  // and make sure the cancel is received.
  stream_->cancel();
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
  autonomous_upstream_ = false;
  initialize();

  default_request_headers_.header_map_->setFormatter(
      std::make_unique<
          Extensions::Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter>(
          false, envoy::extensions::http::header_formatters::preserve_case::v3::
                     PreserveCaseFormatterConfig::DEFAULT));

  default_request_headers_.addCopy("FoO", "bar");
  default_request_headers_.header_map_->formatter().value().get().processKey("FoO");

  stream_prototype_->setOnHeaders(
      [this](Platform::ResponseHeadersSharedPtr headers, bool, envoy_stream_intel) {
        cc_.status = absl::StrCat(headers->httpStatus());
        cc_.on_headers_calls++;
        EXPECT_TRUE(headers->contains("My-ResponsE-Header"));
        EXPECT_TRUE((*headers)["My-ResponsE-Header"][0] == "foo");
        return nullptr;
      });
  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has preserved cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  EXPECT_TRUE(absl::StrContains(upstream_request, "FoO: bar")) << upstream_request;

  // Send mixed case headers, and verify via setOnHeaders they are received correctly.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nMy-ResponsE-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_P(ClientIntegrationTest, TimeoutOnRequestPath) {
  builder_.setStreamIdleTimeoutSeconds(1);

  autonomous_upstream_ = false;
  initialize();

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), false);

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
  builder_.setStreamIdleTimeoutSeconds(1);
  autonomous_upstream_ = false;
  initialize();

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);

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

TEST_P(ClientIntegrationTest, Proxying) {
  builder_.addLogLevel(Platform::LogLevel::trace);
  initialize();

  set_proxy_settings(rawEngine(), fake_upstreams_[0]->localAddress()->asString().c_str(),
                     fake_upstreams_[0]->localAddress()->ip()->port());

  // The initial request will do the DNS lookup.
  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_complete_calls, 1);
  stream_.reset();

  // The second request will use the cached DNS entry and should succeed as well.
  stream_ = (*stream_prototype_).start(explicit_flow_control_);
  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_complete_calls, 2);
}

TEST_P(ClientIntegrationTest, DirectResponse) {
  initialize();
  if (version_ == Network::Address::IpVersion::v6) {
    // Localhost only resolves to an ipv4 address - alas no kernel happy eyeballs.
    return;
  }

  // Override to not validate stream intel.
  stream_prototype_->setOnComplete(
      [this](envoy_stream_intel, envoy_final_stream_intel final_intel) {
        cc_.on_complete_received_byte_count = final_intel.received_byte_count;
        cc_.on_complete_calls++;
        cc_.terminal_callback->setReady();
      });

  default_request_headers_.setHost("127.0.0.1");
  default_request_headers_.setPath("/");

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status, "404");
  ASSERT_EQ(cc_.on_headers_calls, 1);
  stream_.reset();

  // Verify the default runtime values.
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
}

TEST_P(ClientIntegrationTest, TestRuntimeSet) {
  builder_.setRuntimeGuard("test_feature_true", false);
  builder_.setRuntimeGuard("test_feature_false", true);
  initialize();

  // Verify that the Runtime config values are from the RTDS response.
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
}

TEST_P(ClientIntegrationTest, TestStats) {
  initialize();

  std::string stats = engine_->dumpStats();
  EXPECT_TRUE((absl::StrContains(stats, "runtime.load_success: 1"))) << stats;
}

} // namespace
} // namespace Envoy
