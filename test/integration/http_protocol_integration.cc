#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
std::vector<HttpProtocolTestParams> HttpProtocolIntegrationTest::getProtocolTestParams(
    const std::vector<Http::CodecType>& downstream_protocols,
    const std::vector<Http::CodecType>& upstream_protocols) {
  std::vector<HttpProtocolTestParams> ret;

  bool handled_http2_special_cases_downstream = false;
  bool handled_http2_special_cases_upstream = false;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto downstream_protocol : downstream_protocols) {
      for (auto upstream_protocol : upstream_protocols) {
#ifndef ENVOY_ENABLE_QUIC
        if (downstream_protocol == Http::CodecType::HTTP3 ||
            upstream_protocol == Http::CodecType::HTTP3) {
          ENVOY_LOG_MISC(warn, "Skipping HTTP/3 as support is compiled out");
          continue;
        }
#endif

        std::vector<Http1ParserImpl> http1_implementations = {Http1ParserImpl::HttpParser};
        if (downstream_protocol == Http::CodecType::HTTP1 ||
            upstream_protocol == Http::CodecType::HTTP1) {
          http1_implementations.push_back(Http1ParserImpl::BalsaParser);
        }

        std::vector<Http2Impl> http2_implementations = {Http2Impl::Nghttp2};
        std::vector<bool> http2_bool_values = {false};
        if ((!handled_http2_special_cases_downstream &&
             downstream_protocol == Http::CodecType::HTTP2) ||
            (!handled_http2_special_cases_upstream &&
             upstream_protocol == Http::CodecType::HTTP2)) {
          http2_implementations.push_back(Http2Impl::Oghttp2);
          http2_bool_values.push_back(true);

          if (downstream_protocol == Http::CodecType::HTTP2) {
            handled_http2_special_cases_downstream = true;
          }
          if (upstream_protocol == Http::CodecType::HTTP2) {
            handled_http2_special_cases_upstream = true;
          }
        }

        std::vector<bool> use_header_validator_values;
#ifdef ENVOY_ENABLE_UHV
        use_header_validator_values.push_back(true);
#else
        use_header_validator_values.push_back(false);
#endif
        for (Http1ParserImpl http1_implementation : http1_implementations) {
          for (Http2Impl http2_implementation : http2_implementations) {
            for (bool defer_processing : http2_bool_values) {
              for (bool use_header_validator : use_header_validator_values) {
                ret.push_back(HttpProtocolTestParams{
                    ip_version, downstream_protocol, upstream_protocol, http1_implementation,
                    http2_implementation, defer_processing, use_header_validator});
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

std::string HttpProtocolIntegrationTest::protocolTestParamsToString(
    const ::testing::TestParamInfo<HttpProtocolTestParams>& params) {
  return absl::StrCat((params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
                      downstreamToString(params.param.downstream_protocol),
                      upstreamToString(params.param.upstream_protocol),
                      TestUtility::http1ParserImplToString(params.param.http1_implementation),
                      http2ImplementationToString(params.param.http2_implementation),
                      params.param.defer_processing_backedup_streams ? "WithDeferredProcessing"
                                                                     : "NoDeferredProcessing",
                      params.param.use_universal_header_validator ? "Uhv" : "Legacy");
}

void HttpProtocolIntegrationTest::setUpstreamOverrideStreamErrorOnInvalidHttpMessage() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    if (upstreamProtocol() == Http::CodecType::HTTP2) {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http2_protocol_options()
          ->mutable_override_stream_error_on_invalid_http_message()
          ->set_value(true);
    } else {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http3_protocol_options()
          ->mutable_override_stream_error_on_invalid_http_message()
          ->set_value(true);
    }
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
}

void HttpProtocolIntegrationTest::setDownstreamOverrideStreamErrorOnInvalidHttpMessage() {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http3_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
        hcm.mutable_http2_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
        hcm.mutable_http_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
      });
}

} // namespace Envoy
