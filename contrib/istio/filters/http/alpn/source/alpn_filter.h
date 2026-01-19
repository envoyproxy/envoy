#pragma once

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/filters/http/alpn/v3/alpn.pb.h"

namespace Envoy {
namespace Http {
namespace Alpn {

using AlpnOverrides = absl::flat_hash_map<Http::Protocol, std::vector<std::string>>;

class AlpnFilterConfig {
public:
  AlpnFilterConfig(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig& proto_config,
      Upstream::ClusterManager& cluster_manager);

  Upstream::ClusterManager& clusterManager() { return cluster_manager_; }

  const std::vector<std::string> alpnOverrides(const Http::Protocol& protocol) const {
    if (alpn_overrides_.count(protocol)) {
      return alpn_overrides_.at(protocol);
    }
    return {};
  }

private:
  Http::Protocol getHttpProtocol(
      const istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig::Protocol& protocol);

  AlpnOverrides alpn_overrides_;
  Upstream::ClusterManager& cluster_manager_;
};

using AlpnFilterConfigSharedPtr = std::shared_ptr<AlpnFilterConfig>;

class AlpnFilter : public Http::PassThroughDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit AlpnFilter(const AlpnFilterConfigSharedPtr& config) : config_(config) {}

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  const AlpnFilterConfigSharedPtr config_;
};

} // namespace Alpn
} // namespace Http
} // namespace Envoy
