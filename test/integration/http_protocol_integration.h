#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

struct HttpProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecType downstream_protocol;
  Http::CodecType upstream_protocol;
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
    setupHttp2Overrides(GetParam().http2_implementation);
    config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                      GetParam().defer_processing_backedup_streams ? "true"
                                                                                   : "false");
  }

  void SetUp() override {
    setDownstreamProtocol(GetParam().downstream_protocol);
    setUpstreamProtocol(GetParam().upstream_protocol);
  }

protected:
  struct BytesCountExpectation {
    BytesCountExpectation(int wire_bytes_sent, int wire_bytes_received, int header_bytes_sent,
                          int header_bytes_received)
        : wire_bytes_sent_{wire_bytes_sent}, wire_bytes_received_{wire_bytes_received},
          header_bytes_sent_{header_bytes_sent}, header_bytes_received_{header_bytes_received} {}
    int wire_bytes_sent_;
    int wire_bytes_received_;
    int header_bytes_sent_;
    int header_bytes_received_;
  };

  void expectUpstreamBytesSentAndReceived(BytesCountExpectation h1_expectation,
                                          BytesCountExpectation h2_expectation,
                                          BytesCountExpectation h3_expectation, const int id = 0);

  void expectDownstreamBytesSentAndReceived(BytesCountExpectation h1_expectation,
                                            BytesCountExpectation h2_expectation,
                                            BytesCountExpectation h3_expectation, const int id = 0);
};

} // namespace Envoy
