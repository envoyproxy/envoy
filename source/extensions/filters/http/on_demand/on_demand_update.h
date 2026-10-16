#pragma once

#include <optional>

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

/**
 * All on-demand filter stats. Route discovery covers on-demand VHDS and on-demand SRDS, which the
 * filter cannot tell apart. @see stats_macros.h
 */
#define ALL_ON_DEMAND_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                      \
  COUNTER(route_discovery_requested)                                                               \
  COUNTER(route_discovery_found)                                                                   \
  COUNTER(route_discovery_not_found)                                                               \
  COUNTER(route_discovery_cancelled)                                                               \
  COUNTER(cluster_discovery_requested)                                                             \
  COUNTER(cluster_discovery_available)                                                             \
  COUNTER(cluster_discovery_missing)                                                               \
  COUNTER(cluster_discovery_timeout)                                                               \
  COUNTER(cluster_discovery_cancelled)                                                             \
  GAUGE(route_discovery_pending, Accumulate)                                                       \
  GAUGE(cluster_discovery_pending, Accumulate)                                                     \
  HISTOGRAM(route_discovery_duration, Milliseconds)                                                \
  HISTOGRAM(cluster_discovery_duration, Milliseconds)

/**
 * Wrapper struct for on-demand filter stats. @see stats_macros.h
 */
struct OnDemandFilterStats {
  ALL_ON_DEMAND_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                             GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Keys of the monotonic timestamps the filter latches into StreamInfo::downstreamTiming(), for use
 * with %COMMON_DURATION(start:end:precision)%. A key is only set if the corresponding discovery
 * was actually requested.
 */
namespace Timing {
constexpr absl::string_view RouteDiscoveryStart =
    "envoy.filters.http.on_demand.route_discovery_start";
constexpr absl::string_view RouteDiscoveryEnd = "envoy.filters.http.on_demand.route_discovery_end";
constexpr absl::string_view ClusterDiscoveryStart =
    "envoy.filters.http.on_demand.cluster_discovery_start";
constexpr absl::string_view ClusterDiscoveryEnd =
    "envoy.filters.http.on_demand.cluster_discovery_end";
} // namespace Timing

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
  // Same as above, and creates the filter stats as "<stats_prefix>on_demand.<name>" in scope.
  OnDemandFilterConfig(DecodeHeadersBehaviorPtr behavior, Stats::Scope& scope,
                       const std::string& stats_prefix);
  // Constructs config from extension's proto config.
  OnDemandFilterConfig(
      const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
      Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
      absl::Status& creation_status);
  // Same as above, and creates the filter stats as "<stats_prefix>on_demand.<name>" in scope.
  OnDemandFilterConfig(
      const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
      Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
      absl::Status& creation_status, Stats::Scope& scope, const std::string& stats_prefix);
  // Constructs config from extension's per-route proto config.
  OnDemandFilterConfig(
      const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
      Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
      absl::Status& creation_status);

  DecodeHeadersBehavior& decodeHeadersBehavior() const { return *behavior_; }

  // Only the filter-level config has stats; per-route configs only override the behavior.
  OptRef<OnDemandFilterStats> stats() {
    return stats_.has_value() ? OptRef<OnDemandFilterStats>(*stats_) : std::nullopt;
  }

private:
  static OnDemandFilterStats generateStats(Stats::Scope& scope, const std::string& stats_prefix);

  DecodeHeadersBehaviorPtr behavior_;
  std::optional<OnDemandFilterStats> stats_;
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
  MonotonicTime now() const;
  void startRouteDiscovery(MonotonicTime start);
  void finishRouteDiscovery(bool route_exists);
  void startClusterDiscovery(MonotonicTime start);
  void finishClusterDiscovery(Upstream::ClusterDiscoveryStatus cluster_status);

  OnDemandFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_callback_;
  Upstream::ClusterDiscoveryCallbackHandlePtr cluster_discovery_handle_;
  Envoy::Http::FilterHeadersStatus filter_iteration_state_{Http::FilterHeadersStatus::Continue};
  bool decode_headers_active_{false};
  bool downstream_end_stream_{false};
  // Set when the route config update callback ran inside decodeHeaders(), i.e. no asynchronous
  // discovery was started.
  bool route_update_completed_sync_{false};
  // In flight: requested but neither completed nor cancelled by the stream going away.
  bool route_discovery_in_flight_{false};
  bool cluster_discovery_in_flight_{false};
  MonotonicTime route_discovery_start_;
  MonotonicTime cluster_discovery_start_;
};

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
