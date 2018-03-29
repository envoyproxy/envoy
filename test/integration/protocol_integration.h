#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

struct ProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecClient::Type downstream_protocol;
  FakeHttpConnection::Type upstream_protocol;
};

// Allows easy testing of Envoy code for HTTP/HTTP2 upstream/downstream.
//
// Usage:
//
// typedef ProtocolIntegrationTest MyTest
//
// INSTANTIATE_TEST_CASE_P(Protocols, BufferIntegrationTest,
//                        testing::ValuesIn(ProtocolIntegrationTest::GetProtocolTestParams()),
//                        ProtocolIntegrationTest::protocolTestParamsToString);
//
//
// TEST_P(MyTest, TestInstance) {
// ....
// }
class ProtocolIntegrationTest : public HttpIntegrationTest,
                                public testing::TestWithParam<ProtocolTestParams> {
public:
  // Returns 8 combinations of
  // [HTTP  upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP  upstream / HTTP2 downstream] x [Ipv4, IPv6]
  // [HTTP2 upstream / HTTP2 downstream] x [IPv4, Ipv6]
  // [HTTP upstream  / HTTP2 downstream] x [IPv4, Ipv6]
  static std::vector<ProtocolTestParams> GetProtocolTestParams();

  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_Http2Downstream_HttpUpstream
  static std::string
  protocolTestParamsToString(const testing::TestParamInfo<ProtocolTestParams>& p);

  ProtocolIntegrationTest()
      : HttpIntegrationTest(GetParam().downstream_protocol, GetParam().version) {}

  void SetUp() override {
    setDownstreamProtocol(GetParam().downstream_protocol);
    setUpstreamProtocol(GetParam().upstream_protocol);
  }
};

} // namespace Envoy
