#include "extensions/filters/http/alpn/alpn_filter.h"

#include "common/network/application_protocol.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Alpn {

AlpnFilterConfig::AlpnFilterConfig(
    const envoy::config::filter::http::alpn::v2alpha::FilterConfig& proto_config) {
  for (const auto& pair : proto_config.alpn_override()) {
    std::vector<std::string> application_protocols;
    for (const auto& protocol : pair.alpn()) {
      application_protocols.push_back(protocol);
    }

    alpn_override_.insert(
        {getHttpProtocol(pair.downstream_protocol()), std::move(application_protocols)});
  }
}

Http::Protocol AlpnFilterConfig::getHttpProtocol(
    const envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol& protocol) {
  switch (protocol) {
  case envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol::
      FilterConfig_Protocol_HTTP10:
    return Http::Protocol::Http10;
  case envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol::
      FilterConfig_Protocol_HTTP11:
    return Http::Protocol::Http11;
  case envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol::
      FilterConfig_Protocol_HTTP2:
    return Http::Protocol::Http2;
  default:
    // will not reach here.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

Http::FilterHeadersStatus AlpnFilter::decodeHeaders(Http::HeaderMap&, bool) {
  auto protocol = decoder_callbacks_->streamInfo().protocol();
  if (protocol.has_value()) {
    auto alpn_override = config_->getAlpnOverride()[protocol.value()];
    if (!alpn_override.empty()) {
      decoder_callbacks_->streamInfo().filterState().setData(
          Network::ApplicationProtocols::key(),
          std::make_unique<Network::ApplicationProtocols>(alpn_override),
          Envoy::StreamInfo::FilterState::StateType::ReadOnly);
    } else {
      ENVOY_LOG(debug, "alpn override is empty");
    }
  } else {
    ENVOY_LOG(debug, "downstream protocol is not set");
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace Alpn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy