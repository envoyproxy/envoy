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
  bool defer_processing_backedup_streams;
  bool use_universal_header_validator;
  bool tunneling_with_upstream_filters;
};

absl::string_view http2ImplementationToString(Http2Impl impl);

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
    config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                      GetParam().defer_processing_backedup_streams ? "true"
                                                                                   : "false");
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

protected:
  const bool use_universal_header_validator_{false};
};

} // namespace Envoy
