#include "contrib/istio/filters/http/alpn/source/alpn_filter.h"

#include "envoy/upstream/cluster_manager.h"

#include "source/common/network/application_protocol.h"

namespace Envoy {
namespace Http {
namespace Alpn {

AlpnFilterConfig::AlpnFilterConfig(
    const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig& proto_config,
    Upstream::ClusterManager& cluster_manager)
    : cluster_manager_(cluster_manager) {
  for (const auto& pair : proto_config.alpn_override()) {
    std::vector<std::string> application_protocols;
    for (const auto& protocol : pair.alpn_override()) {
      application_protocols.push_back(protocol);
    }

    alpn_overrides_.insert(
        {getHttpProtocol(pair.upstream_protocol()), std::move(application_protocols)});
  }
}

Http::Protocol AlpnFilterConfig::getHttpProtocol(
    const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig::Protocol& protocol) {
  switch (protocol) {
  case istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig::Protocol::
      FilterConfig_Protocol_HTTP10:
    return Http::Protocol::Http10;
  case istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig::Protocol::
      FilterConfig_Protocol_HTTP11:
    return Http::Protocol::Http11;
  case istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig::Protocol::
      FilterConfig_Protocol_HTTP2:
    return Http::Protocol::Http2;
  default:
    PANIC("not implemented");
  }
}

Http::FilterHeadersStatus AlpnFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  const Router::RouteEntry* route_entry;
  if (!route || !(route_entry = route->routeEntry())) {
    ENVOY_LOG(debug, "cannot find route entry");
    return Http::FilterHeadersStatus::Continue;
  }

  Upstream::ThreadLocalCluster* cluster =
      config_->clusterManager().getThreadLocalCluster(route_entry->clusterName());
  if (!cluster || !cluster->info()) {
    ENVOY_LOG(debug, "cannot find cluster {}", route_entry->clusterName());
    return Http::FilterHeadersStatus::Continue;
  }

  const auto& filter_metadata = cluster->info()->metadata().filter_metadata();
  const auto& istio = filter_metadata.find("istio");
  if (istio != filter_metadata.end()) {
    const auto& alpn_override = istio->second.fields().find("alpn_override");
    if (alpn_override != istio->second.fields().end()) {
      const auto alpn_override_value = alpn_override->second.string_value();
      if (alpn_override_value == "false") {
        // Skip ALPN header rewrite
        ENVOY_LOG(debug,
                  "Skipping ALPN header rewrite because istio.alpn_override metadata is false");
        return Http::FilterHeadersStatus::Continue;
      }
    }
  }

  auto protocols =
      cluster->info()->upstreamHttpProtocol(decoder_callbacks_->streamInfo().protocol());
  const auto& alpn_override = config_->alpnOverrides(protocols[0]);

  if (!alpn_override.empty()) {
    ENVOY_LOG(debug, "override with {} ALPNs", alpn_override.size());
    decoder_callbacks_->streamInfo().filterState()->setData(
        Network::ApplicationProtocols::key(),
        std::make_unique<Network::ApplicationProtocols>(alpn_override),
        Envoy::StreamInfo::FilterState::StateType::ReadOnly);
  } else {
    ENVOY_LOG(debug, "ALPN override is empty");
  }
  return Http::FilterHeadersStatus::Continue;
}

} // namespace Alpn
} // namespace Http
} // namespace Envoy
