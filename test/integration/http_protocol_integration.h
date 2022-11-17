#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

struct HttpProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecType downstream_protocol;
  Http::CodecType upstream_protocol;
  Http1Impl http1_implementation;
  Http2Impl http2_implementation;
  bool defer_processing_backedup_streams;
};

// Allows easy testing of Envoy code for HTTP/HTTP2 upstream/downstream.
//
// Usage:
//
// using MyTest = HttpProtocolIntegrationTest;
//
// INSTANTIATE_TEST_SUITE_P(Protocols, MyTest,
//                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
//                         HttpProtocolIntegrationTest::protocolTestParamsToString);
//
//
// TEST_P(MyTest, TestInstance) {
// ....
// }
// TODO(#20996) consider switching to SimulatedTimeSystem instead of using real time.
class HttpProtocolIntegrationTest : public testing::TestWithParam<HttpProtocolTestParams>,
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
  static std::vector<HttpProtocolTestParams> getProtocolTestParams(
      const std::vector<Http::CodecType>& downstream_protocols = {Http::CodecType::HTTP1,
                                                                  Http::CodecType::HTTP2},
      const std::vector<Http::CodecType>& upstream_protocols = {Http::CodecType::HTTP1,
                                                                Http::CodecType::HTTP2});

  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_Http2Downstream_HttpUpstream
  static std::string
  protocolTestParamsToString(const ::testing::TestParamInfo<HttpProtocolTestParams>& p);

  HttpProtocolIntegrationTest()
      : HttpIntegrationTest(
            GetParam().downstream_protocol, GetParam().version,
            ConfigHelper::httpProxyConfig(/*downstream_is_quic=*/GetParam().downstream_protocol ==
                                          Http::CodecType::HTTP3)) {
    setupHttpImplOverrides(GetParam().http1_implementation, GetParam().http2_implementation);
    config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                      GetParam().defer_processing_backedup_streams ? "true"
                                                                                   : "false");
  }

  void SetUp() override {
    setDownstreamProtocol(GetParam().downstream_protocol);
    setUpstreamProtocol(GetParam().upstream_protocol);
  }

  bool skipForH2Uhv() {
#ifdef ENVOY_ENABLE_UHV
    // Validation of upstream responses is not wired up yet
    return GetParam().http2_implementation == Http2Impl::Oghttp2;
#endif
    return false;
  }
};

class UpstreamDownstreamIntegrationTest
    : public testing::TestWithParam<std::tuple<HttpProtocolTestParams, bool>>,
      public HttpIntegrationTest {
public:
  UpstreamDownstreamIntegrationTest()
      : HttpIntegrationTest(
            std::get<0>(GetParam()).downstream_protocol, std::get<0>(GetParam()).version,
            ConfigHelper::httpProxyConfig(std::get<0>(GetParam()).downstream_protocol ==
                                          Http::CodecType::HTTP3)) {
    setupHttpImplOverrides(std::get<0>(GetParam()).http1_implementation,
                           std::get<0>(GetParam()).http2_implementation);
    config_helper_.addRuntimeOverride(
        Runtime::defer_processing_backedup_streams,
        std::get<0>(GetParam()).defer_processing_backedup_streams ? "true" : "false");
  }
  static std::string testParamsToString(
      const ::testing::TestParamInfo<std::tuple<HttpProtocolTestParams, bool>>& params) {
    return fmt::format(
        "{}_{}",
        HttpProtocolIntegrationTest::protocolTestParamsToString(
            ::testing::TestParamInfo<HttpProtocolTestParams>(std::get<0>(params.param), 0)),
        std::get<1>(params.param) ? "DownstreamFilter" : "UpstreamFilter");
  }

  static std::vector<std::tuple<HttpProtocolTestParams, bool>> getDefaultTestParams(
      const std::vector<Http::CodecType>& downstream_protocols = {Http::CodecType::HTTP1,
                                                                  Http::CodecType::HTTP2},
      const std::vector<Http::CodecType>& upstream_protocols = {Http::CodecType::HTTP1,
                                                                Http::CodecType::HTTP2}) {
    std::vector<std::tuple<HttpProtocolTestParams, bool>> ret;
    std::vector<HttpProtocolTestParams> protocol_defaults =
        HttpProtocolIntegrationTest::getProtocolTestParams(downstream_protocols,
                                                           upstream_protocols);
    for (auto& param : protocol_defaults) {
      ret.push_back(std::make_tuple(param, true));
      ret.push_back(std::make_tuple(param, false));
    }
    return ret;
  }

  void SetUp() override {
    setDownstreamProtocol(std::get<0>(GetParam()).downstream_protocol);
    setUpstreamProtocol(std::get<0>(GetParam()).upstream_protocol);
    testing_downstream_filter_ = std::get<1>(GetParam());
  }

  bool testing_downstream_filter_;
};

} // namespace Envoy
