#pragma once

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

class OnDemandFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  OnDemandFilterConfig(Upstream::ClusterManager& cm, Upstream::OdCdsApiSharedPtr odcds);
  OnDemandFilterConfig(
      const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
      Upstream::ClusterManager& cm, Stats::Scope& scope,
      ProtobufMessage::ValidationVisitor& validation_visitor);
  OnDemandFilterConfig(
      const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
      Upstream::ClusterManager& cm, Stats::Scope& scope,
      ProtobufMessage::ValidationVisitor& validation_visitor);

  Upstream::ClusterManager& clusterManager() const { return cm_; }
  Upstream::OdCdsApiSharedPtr odcds() const { return odcds_; }

private:
  Upstream::ClusterManager& cm_;
  Upstream::OdCdsApiSharedPtr odcds_;
};

using OnDemandFilterConfigSharedPtr = std::shared_ptr<OnDemandFilterConfig>;

class OnDemandRouteUpdate : public Http::StreamDecoderFilter {
public:
  OnDemandRouteUpdate(OnDemandFilterConfigSharedPtr config) : config_(std::move(config)) {}

  void onRouteConfigUpdateCompletion(bool route_exists);
  void onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus cluster_status);
  void handleOnDemandCDS(const Router::Route& route);
  const OnDemandFilterConfig* getConfig(const Router::RouteEntry& entry);

  void setFilterIterationState(Envoy::Http::FilterHeadersStatus status) {
    filter_iteration_state_ = status;
  }

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  void onDestroy() override;

private:
  OnDemandFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_callback_;
  Upstream::ClusterDiscoveryCallbackHandlePtr cluster_discovery_handle_;
  Upstream::ClusterDiscoveryCallbackSharedPtr cluster_discovery_callback_;
  Envoy::Http::FilterHeadersStatus filter_iteration_state_{Http::FilterHeadersStatus::Continue};
  bool decode_headers_active_{false};
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
