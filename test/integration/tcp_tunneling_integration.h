#pragma once

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

struct TcpTunnelingTestParams {
  Network::Address::IpVersion version;
  Http::CodecType downstream_protocol;
  Http::CodecType upstream_protocol;
  Http1ParserImpl http1_implementation;
  Http2Impl http2_implementation;
  bool use_universal_header_validator;
  bool tunneling_with_upstream_filters;
};

absl::string_view http2ImplementationToString(Http2Impl impl);

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

// Allows easy testing of Envoy code for HTTP/HTTP2 upstream/downstream.
//
// Usage:
//
// using MyTest = TcpTunnelingIntegrationTest;
//
// INSTANTIATE_TEST_SUITE_P(Protocols, MyTest,
//                         testing::ValuesIn(TcpTunnelingIntegrationTest::getProtocolTestParams()),
//                         TcpTunnelingIntegrationTest::protocolTestParamsToString);
//
//
// TEST_P(MyTest, TestInstance) {
// ....
// }
// TODO(#20996) consider switching to SimulatedTimeSystem instead of using real time.
class BaseTcpTunnelingIntegrationTest : public testing::TestWithParam<TcpTunnelingTestParams>,
                                        public HttpIntegrationTest {
public:
  // By default returns 8 combinations of
  // [HTTP  upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP  upstream / HTTP2 downstream] x [IPv4, Ipv6]
  // [HTTP2 upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP2 upstream / HTTP2 downstream] x [IPv4, Ipv6]
  //
  // Upstream and downstream protocols may be changed via the input vectors.
  // Address combinations are propagated from TestEnvironment::getIpVersionsForTest()
  static std::vector<TcpTunnelingTestParams>
  getProtocolTestParams(const std::vector<Http::CodecType>& downstream_protocols =
                            {
                                Http::CodecType::HTTP1,
                                Http::CodecType::HTTP2,
                                Http::CodecType::HTTP3,
                            },
                        const std::vector<Http::CodecType>& upstream_protocols = {
                            Http::CodecType::HTTP1,
                            Http::CodecType::HTTP2,
                            Http::CodecType::HTTP3,
                        });

  static std::vector<TcpTunnelingTestParams> getProtocolTestParamsWithoutHTTP3() {
    return getProtocolTestParams(
        /*downstream_protocols = */ {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
        /*upstream_protocols = */ {Http::CodecType::HTTP1, Http::CodecType::HTTP2});
  }

  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_Http2Downstream_HttpUpstream
  static std::string
  protocolTestParamsToString(const ::testing::TestParamInfo<TcpTunnelingTestParams>& p);

  BaseTcpTunnelingIntegrationTest()
      : BaseTcpTunnelingIntegrationTest(ConfigHelper::httpProxyConfig(
            /*downstream_is_quic=*/GetParam().downstream_protocol == Http::CodecType::HTTP3)) {}

  BaseTcpTunnelingIntegrationTest(const std::string config)
      : HttpIntegrationTest(GetParam().downstream_protocol, GetParam().version, config),
        use_universal_header_validator_(GetParam().use_universal_header_validator) {
    setupHttp1ImplOverrides(GetParam().http1_implementation);
    setupHttp2ImplOverrides(GetParam().http2_implementation);
    config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_universal_header_validator",
                                      GetParam().use_universal_header_validator ? "true" : "false");
    config_helper_.addRuntimeOverride(Runtime::upstream_http_filters_with_tcp_proxy,
                                      GetParam().tunneling_with_upstream_filters ? "true"
                                                                                 : "false");
  }

  void SetUp() override {
    setDownstreamProtocol(GetParam().downstream_protocol);
    setUpstreamProtocol(GetParam().upstream_protocol);
  }

  void setDownstreamOverrideStreamErrorOnInvalidHttpMessage();
  void setUpstreamOverrideStreamErrorOnInvalidHttpMessage();

  void addHttpUpstreamFilterToCluster(const HttpFilterProto& config) {
    config_helper_.addConfigModifier([config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      ConfigHelper::HttpProtocolOptions protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      *protocol_options.add_http_filters() = config;
      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(protocol_options);
    });
  }

  const HttpFilterProto getCodecFilterConfig() {
    HttpFilterProto filter_config;
    filter_config.set_name("envoy.filters.http.upstream_codec");
    auto configuration = envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec();
    filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  void setUpConnection(FakeHttpConnectionPtr& fake_upstream_connection) {
    // Start a connection, and verify the upgrade headers are received upstream.
    tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
    if (!fake_upstream_connection) {
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection));
    }
    ASSERT_TRUE(fake_upstream_connection->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

    // Send upgrade headers downstream, fully establishing the connection.
    upstream_request_->encodeHeaders(default_response_headers_, false);
  }

  void sendBidiData(FakeHttpConnectionPtr& fake_upstream_connection, bool send_goaway = false) {
    // Send some data from downstream to upstream, and make sure it goes through.
    ASSERT_TRUE(tcp_client_->write("hello", false));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

    if (send_goaway) {
      fake_upstream_connection->encodeGoAway();
    }
    // Send data from upstream to downstream.
    upstream_request_->encodeData(12, false);
    ASSERT_TRUE(tcp_client_->waitForData(12));
  }

  void closeConnection(FakeHttpConnectionPtr& fake_upstream_connection) {
    // Now send more data and close the TCP client. This should be treated as half close, so the
    // data should go through.
    ASSERT_TRUE(tcp_client_->write("hello", false));
    tcp_client_->close();
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
    } else {
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
      // If the upstream now sends 'end stream' the connection is fully closed.
      upstream_request_->encodeData(0, true);
    }
  }

protected:
  const bool use_universal_header_validator_{false};
  IntegrationTcpClientPtr tcp_client_;
};

} // namespace Envoy
