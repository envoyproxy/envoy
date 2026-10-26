#pragma once

#include <optional>
#include <string>

#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/upstream/cluster_manager.h"

#include "absl/container/flat_hash_map.h"
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
 * Request filter state with the details of the discoveries a request waited for, for access logs
 * (%FILTER_STATE(envoy.filters.http.on_demand.discovery:FIELD:<field>)%) and other filters. It only
 * exists if a discovery started, and unset fields read as absent. Fields: host (:authority), vhost
 * (discovered virtual host, "<route config>/<domain>" for VHDS), cluster, route_status,
 * cluster_status, route_discovery_duration_us and cluster_discovery_duration_us (set for every
 * outcome once finished).
 */
class DiscoveryState : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view key() { return "envoy.filters.http.on_demand.discovery"; }

  // The string views returned by getField() point at these literals.
  static constexpr absl::string_view Pending = "pending";
  static constexpr absl::string_view Cancelled = "cancelled";
  static constexpr absl::string_view Found = "found";
  static constexpr absl::string_view NotFound = "not_found";
  static constexpr absl::string_view Available = "available";
  static constexpr absl::string_view Missing = "missing";
  static constexpr absl::string_view Timeout = "timeout";

  // The status is one of the constants above. A finished discovery carries a duration.
  void setRoute(absl::string_view status, std::optional<int64_t> duration_us = std::nullopt) {
    route_status_ = status;
    route_duration_us_ = duration_us;
  }
  void setCluster(absl::string_view status, std::optional<int64_t> duration_us = std::nullopt) {
    cluster_status_ = status;
    cluster_duration_us_ = duration_us;
  }
  void setHost(absl::string_view host) { host_ = std::string(host); }
  void setVirtualHost(absl::string_view vhost) { vhost_ = std::string(vhost); }
  void setClusterName(absl::string_view cluster) { cluster_ = std::string(cluster); }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override {
    return absl::StrCat("route=", route_status_.empty() ? "-" : route_status_,
                        ",cluster=", cluster_status_.empty() ? "-" : cluster_status_);
  }
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override {
    const auto& fields = fieldGetters();
    const auto it = fields.find(field_name);
    return it == fields.end() ? FieldType{absl::monostate{}} : it->second(*this);
  }

private:
  using FieldGetter = FieldType (*)(const DiscoveryState&);

  // All the fields getField() supports in one place: adding one is a single line.
  static const absl::flat_hash_map<absl::string_view, FieldGetter>& fieldGetters() {
    static const absl::flat_hash_map<absl::string_view, FieldGetter> Getters{
        {"host", [](const DiscoveryState& s) { return stringField(s.host_); }},
        {"vhost", [](const DiscoveryState& s) { return stringField(s.vhost_); }},
        {"cluster", [](const DiscoveryState& s) { return stringField(s.cluster_); }},
        {"route_status", [](const DiscoveryState& s) { return stringField(s.route_status_); }},
        {"cluster_status", [](const DiscoveryState& s) { return stringField(s.cluster_status_); }},
        {"route_discovery_duration_us",
         [](const DiscoveryState& s) { return durationField(s.route_duration_us_); }},
        {"cluster_discovery_duration_us",
         [](const DiscoveryState& s) { return durationField(s.cluster_duration_us_); }},
    };
    return Getters;
  }

  static FieldType stringField(absl::string_view value) {
    return value.empty() ? FieldType{absl::monostate{}} : FieldType{value};
  }
  static FieldType durationField(std::optional<int64_t> duration_us) {
    return duration_us.has_value() ? FieldType{*duration_us} : FieldType{absl::monostate{}};
  }

  std::string host_;
  std::string vhost_;
  std::string cluster_;
  absl::string_view route_status_;
  absl::string_view cluster_status_;
  std::optional<int64_t> route_duration_us_;
  std::optional<int64_t> cluster_duration_us_;
};

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
  // The request's DiscoveryState, created on first use.
  DiscoveryState& discoveryState();
  void startRouteDiscovery(MonotonicTime start);
  void finishRouteDiscovery(bool route_exists);
  // Records which virtual host the route discovery resolved to, if any.
  void recordVirtualHost();
  void startClusterDiscovery(MonotonicTime start, absl::string_view cluster_name);
  void finishClusterDiscovery(Upstream::ClusterDiscoveryStatus cluster_status);

  OnDemandFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_callback_;
  Upstream::ClusterDiscoveryCallbackHandlePtr cluster_discovery_handle_;
  Envoy::Http::FilterHeadersStatus filter_iteration_state_{Http::FilterHeadersStatus::Continue};
  bool decode_headers_active_{false};
  bool downstream_end_stream_{false};
  // Only valid during decodeHeaders(), which is where a route discovery is started.
  Http::RequestHeaderMap* request_headers_{};
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
