#pragma once

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

class OnDemandRouteUpdate;
class DecodeHeadersBehavior;
using DecodeHeadersBehaviorPtr = std::unique_ptr<DecodeHeadersBehavior>;

// DecodeHeadersBehavior implementations are used by OnDemandRouteUpdate in its decodeHeaders
// method.
class DecodeHeadersBehavior {
public:
  // The returned object will only perform route discovery if it's missing.
  static DecodeHeadersBehaviorPtr rds();
  // The returned object will perform route discovery if it's missing,
  // then after route is successfully discovered, it will proceed to
  // on-demand cluster discovery if the cluster is missing. The latter
  // discovery will be done with the passed OdCdsApi and timeout.
  static DecodeHeadersBehaviorPtr cdsRds(Upstream::OdCdsApiHandlePtr odcds,
                                         std::chrono::milliseconds timeout);

  virtual ~DecodeHeadersBehavior() = default;

  virtual void decodeHeaders(OnDemandRouteUpdate& filter) PURE;
};

using DecodeHeadersBehaviorPtr = std::unique_ptr<DecodeHeadersBehavior>;

// OnDemandFilterConfig contains information from either the extension's
// proto config or the extension's per-route proto config.
class OnDemandFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit OnDemandFilterConfig(DecodeHeadersBehaviorPtr behavior);
  // Constructs config from extension's proto config.
  OnDemandFilterConfig(
      const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
      Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor);
  // Constructs config from extension's per-route proto config.
  OnDemandFilterConfig(
      const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
      Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor);

  DecodeHeadersBehavior& decodeHeadersBehavior() const { return *behavior_; }

private:
  DecodeHeadersBehaviorPtr behavior_;
};

using OnDemandFilterConfigSharedPtr = std::shared_ptr<OnDemandFilterConfig>;

class OnDemandRouteUpdate : public Http::StreamDecoderFilter {
public:
  OnDemandRouteUpdate(OnDemandFilterConfigSharedPtr config);

  // Callback invoked when route config update is finished.
  void onRouteConfigUpdateCompletion(bool route_exists);
  // Callback invoked when on-demand cluster discovery is finished.
  void onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus cluster_status);
  // Do on-demand route discovery if the route is missing.
  OptRef<const Router::Route> handleMissingRoute();
  // Do on-demand cluster discovery if the cluster used by the route
  // is missing.
  void handleOnDemandCds(const Router::Route& route, Upstream::OdCdsApiHandle& odcds,
                         std::chrono::milliseconds timeout);
  // Get the per-route config if it's available, otherwise the
  // extension's config.
  const OnDemandFilterConfig* getConfig();

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
  Envoy::Http::FilterHeadersStatus filter_iteration_state_{Http::FilterHeadersStatus::Continue};
  bool decode_headers_active_{false};
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
