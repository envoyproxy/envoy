#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
std::vector<HttpProtocolTestParams> HttpProtocolIntegrationTest::getProtocolTestParams(
    const std::vector<Http::CodecClient::Type>& downstream_protocols,
    const std::vector<FakeHttpConnection::Type>& upstream_protocols) {
  std::vector<HttpProtocolTestParams> ret;

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto downstream_protocol : downstream_protocols) {
      for (auto upstream_protocol : upstream_protocols) {
#ifdef ENVOY_ENABLE_QUIC
        ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol});
#else
        if (downstream_protocol == Http::CodecClient::Type::HTTP3 ||
            upstream_protocol == FakeHttpConnection::Type::HTTP3) {
          ENVOY_LOG_MISC(warn, "Skipping HTTP/3 as support is compiled out");
        } else {
          ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol});
        }
#endif
      }
    }
  }
  return ret;
}

absl::string_view upstreamToString(FakeHttpConnection::Type type) {
  switch (type) {
  case FakeHttpConnection::Type::HTTP1:
    return "HttpUpstream";
  case FakeHttpConnection::Type::HTTP2:
    return "Http2Upstream";
  case FakeHttpConnection::Type::HTTP3:
    return "Http3Upstream";
  }
  return "UnknownUpstream";
}

absl::string_view downstreamToString(Http::CodecClient::Type type) {
  switch (type) {
  case Http::CodecClient::Type::HTTP1:
    return "HttpDownstream_";
  case Http::CodecClient::Type::HTTP2:
    return "Http2Downstream_";
  case Http::CodecClient::Type::HTTP3:
    return "Http3Downstream_";
  }
  return "UnknownDownstream";
}

std::string HttpProtocolIntegrationTest::protocolTestParamsToString(
    const ::testing::TestParamInfo<HttpProtocolTestParams>& params) {
  return absl::StrCat((params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
                      downstreamToString(params.param.downstream_protocol),
                      upstreamToString(params.param.upstream_protocol));
}

} // namespace Envoy
