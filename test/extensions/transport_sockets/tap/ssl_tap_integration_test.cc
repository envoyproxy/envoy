#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"
#include "envoy/extensions/transport_sockets/tap/v3/tap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/network/connection_impl.h"

#include "test/common/tls/integration/ssl_integration_test_base.h"
#include "test/extensions/common/tap/common.h"

namespace Envoy {
namespace Ssl {

// TODO(zuercher): write an additional OCSP integration test that validates behavior with an
// expired OCSP response. (Requires OCSP client-side support in upstream TLS.)
class SslTapIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public SslIntegrationTestBase {
public:
  SslTapIntegrationTest() : SslIntegrationTestBase(GetParam()) {}

  void TearDown() override { SslIntegrationTestBase::TearDown(); };

  void initialize() override {
    // TODO(mattklein123): Merge/use the code in ConfigHelper::setTapTransportSocket().
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // The test supports tapping either the downstream or upstream connection, but not both.
      if (upstream_tap_) {
        setupUpstreamTap(bootstrap);
      } else {
        setupDownstreamTap(bootstrap);
      }
    });
    SslIntegrationTestBase::initialize();
    // This confuses our socket counting.
    debug_with_s_client_ = false;
  }

  void setupUpstreamTap(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* transport_socket =
        bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
    transport_socket->set_name("envoy.transport_sockets.tap");
    envoy::config::core::v3::TransportSocket raw_transport_socket;
    raw_transport_socket.set_name("envoy.transport_sockets.raw_buffer");
    envoy::extensions::transport_sockets::tap::v3::Tap tap_config =
        createTapConfig(raw_transport_socket);
    tap_config.mutable_transport_socket()->MergeFrom(raw_transport_socket);
    transport_socket->mutable_typed_config()->PackFrom(tap_config);
  }

  void setupDownstreamTap(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    // Configure inner SSL transport socket based on existing config.
    envoy::config::core::v3::TransportSocket ssl_transport_socket;
    auto* transport_socket = filter_chain->mutable_transport_socket();
    ssl_transport_socket.Swap(transport_socket);
    // Configure outer tap transport socket.
    transport_socket->set_name("envoy.transport_sockets.tap");
    envoy::extensions::transport_sockets::tap::v3::Tap tap_config =
        createTapConfig(ssl_transport_socket);
    tap_config.mutable_transport_socket()->MergeFrom(ssl_transport_socket);
    transport_socket->mutable_typed_config()->PackFrom(tap_config);
  }

  envoy::extensions::transport_sockets::tap::v3::Tap
  createTapConfig(const envoy::config::core::v3::TransportSocket& inner_transport) {
    envoy::extensions::transport_sockets::tap::v3::Tap tap_config;
    tap_config.mutable_common_config()->mutable_static_config()->mutable_match()->set_any_match(
        true);
    auto* output_config =
        tap_config.mutable_common_config()->mutable_static_config()->mutable_output_config();
    if (max_rx_bytes_.has_value()) {
      output_config->mutable_max_buffered_rx_bytes()->set_value(max_rx_bytes_.value());
    }
    if (max_tx_bytes_.has_value()) {
      output_config->mutable_max_buffered_tx_bytes()->set_value(max_tx_bytes_.value());
    }
    output_config->set_streaming(streaming_tap_);

    auto* output_sink = output_config->mutable_sinks()->Add();
    output_sink->set_format(format_);
    output_sink->mutable_file_per_tap()->set_path_prefix(path_prefix_);
    tap_config.mutable_transport_socket()->MergeFrom(inner_transport);
    return tap_config;
  }

  std::string path_prefix_ = TestEnvironment::temporaryPath("ssl_trace");
  envoy::config::tap::v3::OutputSink::Format format_{
      envoy::config::tap::v3::OutputSink::PROTO_BINARY};
  absl::optional<uint64_t> max_rx_bytes_;
  absl::optional<uint64_t> max_tx_bytes_;
  bool upstream_tap_{};
  bool streaming_tap_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslTapIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Validate two back-to-back requests with binary proto output.
TEST_P(SslTapIntegrationTest, TwoRequestsWithBinaryProto) {
  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // First request (ID will be +1 since the client will also bump).
  const uint64_t first_id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  codec_client_ = makeHttpConnection(creator());
  Http::TestRequestHeaderMapImpl post_request_headers{
      {":method", "POST"},       {":path", "/test/long/url"},
      {":scheme", "http"},       {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
  auto response =
      sendRequestAndWaitForResponse(post_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(256, response->body().size());
  checkStats();
  envoy::config::core::v3::Address expected_local_address;
  Network::Utility::addressToProtobufAddress(
      *codec_client_->connection()->connectionInfoProvider().remoteAddress(),
      expected_local_address);
  envoy::config::core::v3::Address expected_remote_address;
  Network::Utility::addressToProtobufAddress(
      *codec_client_->connection()->connectionInfoProvider().localAddress(),
      expected_remote_address);
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::data::tap::v3::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, first_id), trace, *api_);
  // Validate general expected properties in the trace.
  EXPECT_EQ(first_id, trace.socket_buffered_trace().trace_id());
  EXPECT_THAT(expected_local_address,
              ProtoEq(trace.socket_buffered_trace().connection().local_address()));
  EXPECT_THAT(expected_remote_address,
              ProtoEq(trace.socket_buffered_trace().connection().remote_address()));
  ASSERT_GE(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data().as_bytes(),
                               "POST /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(1).write().data().as_bytes(),
                               "HTTP/1.1 200 OK"));
  EXPECT_FALSE(trace.socket_buffered_trace().read_truncated());
  EXPECT_FALSE(trace.socket_buffered_trace().write_truncated());

  // Verify a second request hits a different file.
  const uint64_t second_id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  codec_client_ = makeHttpConnection(creator());
  Http::TestRequestHeaderMapImpl get_request_headers{
      {":method", "GET"},        {":path", "/test/long/url"},
      {":scheme", "http"},       {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
  response =
      sendRequestAndWaitForResponse(get_request_headers, 128, default_response_headers_, 256);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(128, upstream_request_->bodyLength());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(256, response->body().size());
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 2);
  TestUtility::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, second_id), trace, *api_);
  // Validate second connection ID.
  EXPECT_EQ(second_id, trace.socket_buffered_trace().trace_id());
  ASSERT_GE(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data().as_bytes(),
                               "GET /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(1).write().data().as_bytes(),
                               "HTTP/1.1 200 OK"));
  EXPECT_FALSE(trace.socket_buffered_trace().read_truncated());
  EXPECT_FALSE(trace.socket_buffered_trace().write_truncated());
}

// Verify that truncation works correctly across multiple transport socket frames.
TEST_P(SslTapIntegrationTest, TruncationWithMultipleDataFrames) {
  max_rx_bytes_ = 4;
  max_tx_bytes_ = 5;

  initialize();
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  codec_client_ = makeHttpConnection(creator());
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "sni.lyft.com"}};
  auto result = codec_client_->startRequest(request_headers);
  auto response = std::move(result.second);
  Buffer::OwnedImpl data1("one");
  result.first.encodeData(data1, false);
  Buffer::OwnedImpl data2("two");
  result.first.encodeData(data2, true);
  waitForNextUpstreamRequest();
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl data3("three");
  upstream_request_->encodeData(data3, false);
  response->waitForBodyData(5);
  Buffer::OwnedImpl data4("four");
  upstream_request_->encodeData(data4, true);
  ASSERT_TRUE(response->waitForEndStream());

  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);

  envoy::data::tap::v3::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb", path_prefix_, id), trace, *api_);

  ASSERT_EQ(trace.socket_buffered_trace().events().size(), 2);
  EXPECT_TRUE(trace.socket_buffered_trace().events(0).read().data().truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().events(1).write().data().truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().write_truncated());
}

// Validate a single request with text proto output.
TEST_P(SslTapIntegrationTest, RequestWithTextProto) {
  format_ = envoy::config::tap::v3::OutputSink::PROTO_TEXT;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // Disable for this test because it uses connection IDs, which disrupts the accounting below
  // leading to the wrong path for the `pb_text` being used.
  skip_tag_extraction_rule_check_ = true;

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 1;
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  envoy::data::tap::v3::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.pb_text", path_prefix_, id), trace, *api_);
  // Test some obvious properties.
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(0).read().data().as_bytes(),
                               "GET /test/long/url HTTP/1.1"));
  EXPECT_TRUE(absl::StartsWith(trace.socket_buffered_trace().events(1).write().data().as_bytes(),
                               "HTTP/1.1 200 OK"));
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_FALSE(trace.socket_buffered_trace().write_truncated());
}

// Validate a single request with JSON (body as string) output. This test uses an upstream tap.
TEST_P(SslTapIntegrationTest, RequestWithJsonBodyAsStringUpstreamTap) {
  upstream_tap_ = true;
  max_rx_bytes_ = 5;
  max_tx_bytes_ = 4;

  format_ = envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // Disable for this test because it uses connection IDs, which disrupts the accounting below
  // leading to the wrong path for the `pb_text` being used.
  skip_tag_extraction_rule_check_ = true;

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 2;
  testRouterRequestAndResponseWithBody(512, 1024, false, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  test_server_.reset();

  // This must be done after server shutdown so that connection pool connections are closed and
  // the tap written.
  envoy::data::tap::v3::TraceWrapper trace;
  TestUtility::loadFromFile(fmt::format("{}_{}.json", path_prefix_, id), trace, *api_);

  // Test some obvious properties.
  EXPECT_EQ(trace.socket_buffered_trace().events(0).write().data().as_string(), "GET ");
  EXPECT_EQ(trace.socket_buffered_trace().events(1).read().data().as_string(), "HTTP/");
  EXPECT_TRUE(trace.socket_buffered_trace().read_truncated());
  EXPECT_TRUE(trace.socket_buffered_trace().write_truncated());
}

// Validate a single request with length delimited binary proto output. This test uses an upstream
// tap.
TEST_P(SslTapIntegrationTest, RequestWithStreamingUpstreamTap) {
  upstream_tap_ = true;
  streaming_tap_ = true;
  max_rx_bytes_ = 5;
  max_tx_bytes_ = 4;

  format_ = envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // Disable for this test because it uses connection IDs, which disrupts the accounting below
  // leading to the wrong path for the `pb_text` being used.
  skip_tag_extraction_rule_check_ = true;

  const uint64_t id = Network::ConnectionImpl::nextGlobalIdForTest() + 2;
  testRouterRequestAndResponseWithBody(512, 1024, false, false, &creator);
  checkStats();
  codec_client_->close();
  test_server_->waitForCounterGe("http.config_test.downstream_cx_destroy", 1);
  test_server_.reset();

  // This must be done after server shutdown so that connection pool connections are closed and
  // the tap written.
  std::vector<envoy::data::tap::v3::TraceWrapper> traces =
      Extensions::Common::Tap::readTracesFromFile(
          fmt::format("{}_{}.pb_length_delimited", path_prefix_, id));
  ASSERT_GE(traces.size(), 4);

  // The initial connection message has no local address, but has a remote address (not connected
  // yet).
  EXPECT_TRUE(traces[0].socket_streamed_trace_segment().has_connection());
  EXPECT_FALSE(traces[0].socket_streamed_trace_segment().connection().has_local_address());
  EXPECT_TRUE(traces[0].socket_streamed_trace_segment().connection().has_remote_address());

  // Verify truncated request/response data.
  EXPECT_EQ(traces[1].socket_streamed_trace_segment().event().write().data().as_bytes(), "GET ");
  EXPECT_TRUE(traces[1].socket_streamed_trace_segment().event().write().data().truncated());
  EXPECT_EQ(traces[2].socket_streamed_trace_segment().event().read().data().as_bytes(), "HTTP/");
  EXPECT_TRUE(traces[2].socket_streamed_trace_segment().event().read().data().truncated());
}

} // namespace Ssl
} // namespace Envoy
