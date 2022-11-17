#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
std::vector<HttpProtocolTestParams> HttpProtocolIntegrationTest::getProtocolTestParams(
    const std::vector<Http::CodecType>& downstream_protocols,
    const std::vector<Http::CodecType>& upstream_protocols) {
  std::vector<HttpProtocolTestParams> ret;

  const auto addHttp2TestParametersWithDeferredProcessing =
      [&ret](Network::Address::IpVersion ip_version, Http::CodecType downstream_protocol,
             Http::CodecType upstream_protocol) {
        ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                             Http2Impl::Oghttp2, false});
        ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                             Http2Impl::Oghttp2, true});
        ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                             Http2Impl::Nghttp2, true});
      };

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto downstream_protocol : downstream_protocols) {
      for (auto upstream_protocol : upstream_protocols) {
#ifdef ENVOY_ENABLE_QUIC
        ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                             Http2Impl::Nghttp2, false});
        if (downstream_protocol == Http::CodecType::HTTP2 ||
            upstream_protocol == Http::CodecType::HTTP2) {
          addHttp2TestParametersWithDeferredProcessing(ip_version, downstream_protocol,
                                                       upstream_protocol);
        }
#else
        if (downstream_protocol == Http::CodecType::HTTP3 ||
            upstream_protocol == Http::CodecType::HTTP3) {
          ENVOY_LOG_MISC(warn, "Skipping HTTP/3 as support is compiled out");
        } else {
          ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                               Http2Impl::Nghttp2, false});
          if (downstream_protocol == Http::CodecType::HTTP2 ||
              upstream_protocol == Http::CodecType::HTTP2) {
            addHttp2TestParametersWithDeferredProcessing(ip_version, downstream_protocol,
                                                         upstream_protocol);
          }
        }
#endif
      }
    }
  }
  return ret;
}

absl::string_view upstreamToString(Http::CodecType type) {
  switch (type) {
  case Http::CodecType::HTTP1:
    return "HttpUpstream";
  case Http::CodecType::HTTP2:
    return "Http2Upstream";
  case Http::CodecType::HTTP3:
    return "Http3Upstream";
  }
  return "UnknownUpstream";
}

absl::string_view downstreamToString(Http::CodecType type) {
  switch (type) {
  case Http::CodecType::HTTP1:
    return "HttpDownstream_";
  case Http::CodecType::HTTP2:
    return "Http2Downstream_";
  case Http::CodecType::HTTP3:
    return "Http3Downstream_";
  }
  return "UnknownDownstream";
}

absl::string_view implementationToString(Http2Impl impl) {
  switch (impl) {
  case Http2Impl::Nghttp2:
    return "Nghttp2";
  case Http2Impl::Oghttp2:
    return "Oghttp2";
  }
  return "UnknownHttp2Impl";
}

std::string HttpProtocolIntegrationTest::protocolTestParamsToString(
    const ::testing::TestParamInfo<HttpProtocolTestParams>& params) {
  return absl::StrCat((params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
                      downstreamToString(params.param.downstream_protocol),
                      upstreamToString(params.param.upstream_protocol),
                      implementationToString(params.param.http2_implementation),
                      params.param.defer_processing_backedup_streams ? "WithDeferredProcessing"
                                                                     : "NoDeferredProcessing");
}

} // namespace Envoy
