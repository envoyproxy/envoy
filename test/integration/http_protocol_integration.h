#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

struct HttpProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecClient::Type downstream_protocol;
  FakeHttpConnection::Type upstream_protocol;
};

// Allows easy testing of Envoy code for HTTP/HTTP2 upstream/downstream.
//
// Usage:
//
// typedef HttpProtocolIntegrationTest MyTest
//
// INSTANTIATE_TEST_CASE_P(Protocols, BufferIntegrationTest,
//                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
//                         HttpProtocolIntegrationTest::protocolTestParamsToString);
//
//
// TEST_P(MyTest, TestInstance) {
// ....
// }
class HttpProtocolIntegrationTest : public HttpIntegrationTest,
                                    public testing::TestWithParam<HttpProtocolTestParams> {
public:
  // By default returns 8 combinations of
  // [HTTP  upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP  upstream / HTTP2 downstream] x [Ipv4, IPv6]
  // [HTTP2 upstream / HTTP2 downstream] x [IPv4, Ipv6]
  // [HTTP upstream  / HTTP2 downstream] x [IPv4, Ipv6]
  //
  // Upstream and downstream protocols may be changed via the input vectors.
  // Address combinations are propogated from TestEnvironment::getIpVersionsForTest()
  static std::vector<HttpProtocolTestParams>
  getProtocolTestParams(const std::vector<Http::CodecClient::Type>& downstream_protocols =
                            {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                        const std::vector<FakeHttpConnection::Type>& upstream_protocols = {
                            FakeHttpConnection::Type::HTTP1, FakeHttpConnection::Type::HTTP2});

  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_Http2Downstream_HttpUpstream
  static std::string
  protocolTestParamsToString(const testing::TestParamInfo<HttpProtocolTestParams>& p);

  HttpProtocolIntegrationTest()
      : HttpIntegrationTest(GetParam().downstream_protocol, GetParam().version) {}

  void SetUp() override {
    setDownstreamProtocol(GetParam().downstream_protocol);
    setUpstreamProtocol(GetParam().upstream_protocol);
  }
};

} // namespace Envoy
