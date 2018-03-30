#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
std::vector<HttpProtocolTestParams> HttpProtocolIntegrationTest::getProtocolTestParams() {
  std::vector<HttpProtocolTestParams> ret;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    ret.push_back(HttpProtocolTestParams{ip_version, Http::CodecClient::Type::HTTP1,
                                         FakeHttpConnection::Type::HTTP1});
    ret.push_back(HttpProtocolTestParams{ip_version, Http::CodecClient::Type::HTTP2,
                                         FakeHttpConnection::Type::HTTP1});
    ret.push_back(HttpProtocolTestParams{ip_version, Http::CodecClient::Type::HTTP1,
                                         FakeHttpConnection::Type::HTTP2});
    ret.push_back(HttpProtocolTestParams{ip_version, Http::CodecClient::Type::HTTP2,
                                         FakeHttpConnection::Type::HTTP2});
  }
  return ret;
}

std::string HttpProtocolIntegrationTest::protocolTestParamsToString(
    const ::testing::TestParamInfo<HttpProtocolTestParams>& params) {
  return absl::StrCat(
      (params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
      (params.param.downstream_protocol == Http::CodecClient::Type::HTTP2 ? "Http2Downstream_"
                                                                          : "HttpDownstream_"),
      (params.param.upstream_protocol == FakeHttpConnection::Type::HTTP2 ? "Http2Upstream"
                                                                         : "HttpUpstream"));
}

} // namespace Envoy
