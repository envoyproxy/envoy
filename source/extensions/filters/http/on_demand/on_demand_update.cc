#include "source/extensions/filters/http/on_demand/on_demand_update.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/config/xds_resource.h"
#include "source/common/http/codes.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/upstream/od_cds_api_impl.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

namespace {

class RdsDecodeHeadersBehavior : public DecodeHeadersBehavior {
public:
  void decodeHeaders(OnDemandRouteUpdate& filter) override { filter.handleMissingRoute(); }
};

class RdsCdsDecodeHeadersBehavior : public DecodeHeadersBehavior {
public:
  RdsCdsDecodeHeadersBehavior(Upstream::OdCdsApiHandlePtr odcds, std::chrono::milliseconds timeout)
      : odcds_(std::move(odcds)), timeout_(timeout) {}

  void decodeHeaders(OnDemandRouteUpdate& filter) override {
    auto route = filter.handleMissingRoute();
    if (!route.has_value()) {
      return;
    }
    filter.handleOnDemandCds(route.value(), *odcds_, timeout_);
  }

private:
  Upstream::OdCdsApiHandlePtr odcds_;
  std::chrono::milliseconds timeout_;
};

} // namespace

DecodeHeadersBehaviorPtr DecodeHeadersBehavior::rds() {
  return std::make_unique<RdsDecodeHeadersBehavior>();
}

DecodeHeadersBehaviorPtr DecodeHeadersBehavior::cdsRds(Upstream::OdCdsApiHandlePtr odcds,
                                                       std::chrono::milliseconds timeout) {
  return std::make_unique<RdsCdsDecodeHeadersBehavior>(std::move(odcds), timeout);
}

namespace {

absl::StatusOr<DecodeHeadersBehaviorPtr> createDecodeHeadersBehavior(
    OptRef<const envoy::extensions::filters::http::on_demand::v3::OnDemandCds> odcds_config,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor) {
  if (!odcds_config.has_value()) {
    return DecodeHeadersBehavior::rds();
  }
  Upstream::OdCdsApiHandlePtr odcds = nullptr;
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.xdstp_based_config_singleton_subscriptions")) {
    // For xDS-TP based configs, both the odcds_config->source and
    // odcds_config->resources_locator must be empty.
    if (!odcds_config->has_source() && odcds_config->resources_locator().empty()) {
      auto odcds_or = cm.allocateOdCdsApi(&Upstream::XdstpOdCdsApiImpl::create,
                                          odcds_config->source(), std::nullopt, validation_visitor);
      RETURN_IF_NOT_OK_REF(odcds_or.status());
      odcds = std::move(odcds_or.value());
    }
  }
  // TODO(adisuissa): Once the
  // "envoy.reloadable_features.xdstp_based_config_singleton_subscriptions" runtime flag is
  // deprecated, change "if (odcds == nullptr)" to "else" (and further merge the else with the "if
  // (odcds_config->resources_locator().empty())").
  if (odcds == nullptr) {
    if (odcds_config->resources_locator().empty()) {
      if (odcds_config->source().config_source_specifier_case() ==
          envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds) {
        // If the config-source is ADS, use a singleton-subscription mechanism,
        // similar to xDS-TP based configs.
        auto odcds_or =
            cm.allocateOdCdsApi(&Upstream::XdstpOdCdsApiImpl::create, odcds_config->source(),
                                std::nullopt, validation_visitor);
        RETURN_IF_NOT_OK_REF(odcds_or.status());
        odcds = std::move(odcds_or.value());
      } else {
        auto odcds_or = cm.allocateOdCdsApi(&Upstream::OdCdsApiImpl::create, odcds_config->source(),
                                            std::nullopt, validation_visitor);
        RETURN_IF_NOT_OK_REF(odcds_or.status());
        odcds = std::move(odcds_or.value());
      }
    } else {
      auto locator_or = Config::XdsResourceIdentifier::decodeUrl(odcds_config->resources_locator());
      RETURN_IF_NOT_OK_REF(locator_or.status());
      auto odcds_or = cm.allocateOdCdsApi(&Upstream::OdCdsApiImpl::create, odcds_config->source(),
                                          locator_or.value(), validation_visitor);
      RETURN_IF_NOT_OK_REF(odcds_or.status());
      odcds = std::move(odcds_or.value());
    }
  }
  // If changing the default timeout, please update the documentation in on_demand.proto too.
  auto timeout =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(odcds_config.ref(), timeout, 5000));
  return DecodeHeadersBehavior::cdsRds(std::move(odcds), timeout);
}

template <typename ProtoConfig>
OptRef<const envoy::extensions::filters::http::on_demand::v3::OnDemandCds>
getOdCdsConfig(const ProtoConfig& proto_config) {
  if (!proto_config.has_odcds()) {
    return std::nullopt;
  }
  return proto_config.odcds();
}

} // namespace

OnDemandFilterStats OnDemandFilterConfig::generateStats(Stats::Scope& scope,
                                                        const std::string& stats_prefix) {
  const std::string prefix = absl::StrCat(stats_prefix, "on_demand.");
  return {ALL_ON_DEMAND_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                     POOL_GAUGE_PREFIX(scope, prefix),
                                     POOL_HISTOGRAM_PREFIX(scope, prefix))};
}

OnDemandFilterConfig::OnDemandFilterConfig(DecodeHeadersBehaviorPtr behavior)
    : behavior_(std::move(behavior)) {}

OnDemandFilterConfig::OnDemandFilterConfig(DecodeHeadersBehaviorPtr behavior, Stats::Scope& scope,
                                           const std::string& stats_prefix)
    : behavior_(std::move(behavior)), stats_(generateStats(scope, stats_prefix)) {}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
    absl::Status& creation_status) {
  auto behavior_or =
      createDecodeHeadersBehavior(getOdCdsConfig(proto_config), cm, validation_visitor);
  SET_AND_RETURN_IF_NOT_OK(behavior_or.status(), creation_status);
  behavior_ = std::move(behavior_or.value());
}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
    absl::Status& creation_status, Stats::Scope& scope, const std::string& stats_prefix)
    : OnDemandFilterConfig(proto_config, cm, validation_visitor, creation_status) {
  stats_.emplace(generateStats(scope, stats_prefix));
}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
    absl::Status& creation_status) {
  auto behavior_or =
      createDecodeHeadersBehavior(getOdCdsConfig(proto_config), cm, validation_visitor);
  SET_AND_RETURN_IF_NOT_OK(behavior_or.status(), creation_status);
  behavior_ = std::move(behavior_or.value());
}

OnDemandRouteUpdate::OnDemandRouteUpdate(OnDemandFilterConfigSharedPtr config)
    : config_(std::move(config)) {
  if (config_ == nullptr) {
    // if config is nil, fall back to rds only
    config_ = std::make_shared<OnDemandFilterConfig>(DecodeHeadersBehavior::rds());
  }
}

OptRef<const Router::Route> OnDemandRouteUpdate::handleMissingRoute() {
  if (auto route = callbacks_->route(); route) {
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return *route;
  }
  // decodeHeaders() is interrupted.
  decode_headers_active_ = true;
  route_update_completed_sync_ = false;
  const MonotonicTime start = now();
  route_config_updated_callback_ =
      std::make_shared<Http::RouteConfigUpdatedCallback>(Http::RouteConfigUpdatedCallback(
          [this](bool route_exists) -> void { onRouteConfigUpdateCompletion(route_exists); }));
  filter_iteration_state_ = Http::FilterHeadersStatus::StopIteration;
  callbacks_->downstreamCallbacks()->requestRouteConfigUpdate(route_config_updated_callback_);
  // decodeHeaders() is completed.
  decode_headers_active_ = false;
  // The callback runs synchronously when there is nothing to discover (neither VHDS nor on-demand
  // SRDS applies); only a callback still pending here is an actual discovery.
  if (!route_update_completed_sync_) {
    startRouteDiscovery(start);
  }
  return callbacks_->route();
}

Http::FilterHeadersStatus OnDemandRouteUpdate::decodeHeaders(Http::RequestHeaderMap& headers,
                                                             bool end_stream) {
  downstream_end_stream_ = end_stream;
  request_headers_ = &headers;
  auto config = getConfig();

  config->decodeHeadersBehavior().decodeHeaders(*this);
  request_headers_ = nullptr;
  return filter_iteration_state_;
}

// Passed route pointer here is not null.
void OnDemandRouteUpdate::handleOnDemandCds(const Router::Route& route,
                                            Upstream::OdCdsApiHandle& odcds,
                                            std::chrono::milliseconds timeout) {
  if (callbacks_->clusterInfo().has_value()) {
    // Cluster already exists, so nothing to do here.
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  const auto entry = route.routeEntry();
  if (entry == nullptr) {
    // No entry? Nothing we can do here.
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  const auto cluster_name = entry->clusterName();
  if (cluster_name.empty()) {
    // Empty cluster name may be a result of a missing HTTP header
    // used for getting the cluster name. Nothing we can do here.
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  filter_iteration_state_ = Http::FilterHeadersStatus::StopIteration;
  startClusterDiscovery(now(), cluster_name);
  auto callback = std::make_unique<Upstream::ClusterDiscoveryCallback>(
      [this](Upstream::ClusterDiscoveryStatus cluster_status) {
        onClusterDiscoveryCompletion(cluster_status);
      });
  cluster_discovery_handle_ =
      odcds.requestOnDemandClusterDiscovery(cluster_name, std::move(callback), timeout);
}

const OnDemandFilterConfig* OnDemandRouteUpdate::getConfig() {
  auto config = Http::Utility::resolveMostSpecificPerFilterConfig<OnDemandFilterConfig>(callbacks_);
  if (config != nullptr) {
    return config;
  }
  return config_.get();
}

Http::FilterDataStatus OnDemandRouteUpdate::decodeData(Buffer::Instance&, bool end_stream) {
  downstream_end_stream_ = end_stream;
  return filter_iteration_state_ == Http::FilterHeadersStatus::StopIteration
             ? Http::FilterDataStatus::StopIterationAndWatermark
             : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OnDemandRouteUpdate::decodeTrailers(Http::RequestTrailerMap&) {
  downstream_end_stream_ = true;
  return Http::FilterTrailersStatus::Continue;
}

void OnDemandRouteUpdate::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

// A weak_ptr copy of the route_config_updated_callback_ is kept by RdsRouteConfigProviderImpl in
// config_update_callbacks_. Same about cluster_discovery_callback kept by ClusterDiscoveryManager
// in pending_clusters_. By resetting the pointers in onDestroy() callback we ensure that this
// filter/filter-chain will not be resumed if the corresponding has been closed.
void OnDemandRouteUpdate::onDestroy() {
  route_config_updated_callback_.reset();
  cluster_discovery_handle_.reset();
  // A discovery still in flight here was abandoned, e.g. the client went away while waiting.
  if (route_discovery_in_flight_) {
    discoveryState().setRoute(DiscoveryState::Cancelled);
  }
  if (cluster_discovery_in_flight_) {
    discoveryState().setCluster(DiscoveryState::Cancelled);
  }
  if (auto stats = config_->stats(); stats.has_value()) {
    if (route_discovery_in_flight_) {
      stats->route_discovery_cancelled_.inc();
      stats->route_discovery_pending_.dec();
    }
    if (cluster_discovery_in_flight_) {
      stats->cluster_discovery_cancelled_.inc();
      stats->cluster_discovery_pending_.dec();
    }
  }
  route_discovery_in_flight_ = false;
  cluster_discovery_in_flight_ = false;
}

// This is the callback which is called when an update requested in requestRouteConfigUpdate()
// has been propagated to workers, at which point the request processing is restarted from the
// beginning.
void OnDemandRouteUpdate::onRouteConfigUpdateCompletion(bool route_exists) {
  filter_iteration_state_ = Http::FilterHeadersStatus::Continue;

  // Don't call continueDecoding in the middle of decodeHeaders()
  if (decode_headers_active_) {
    route_update_completed_sync_ = true;
    return;
  }

  finishRouteDiscovery(route_exists);

  // VHDS only reports that the virtual host exists. If none of its routes match the request,
  // requesting the same virtual host again cannot resolve one, so let the router reply instead.
  const bool require_route_match = Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.on_demand_vhds_require_route_match");

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.on_demand_vhds_no_recreate_stream")) {
    // Continue the existing stream so the decoder filters before on_demand do not run again.
    // Refreshing the route config snapshot lets the router see the newly discovered virtual host,
    // and re-running decodeHeaders() starts on-demand CDS if the resolved cluster is unknown. In
    // that case onClusterDiscoveryCompletion() resumes the stream.
    if (route_exists) {
      callbacks_->downstreamCallbacks()->refreshRouteConfigSnapshot();
      callbacks_->downstreamCallbacks()->clearRouteCache();
      recordVirtualHost();
      if (!require_route_match || callbacks_->route().has_value()) {
        getConfig()->decodeHeadersBehavior().decodeHeaders(*this);
        if (filter_iteration_state_ == Http::FilterHeadersStatus::StopIteration) {
          return;
        }
      }
    }
    callbacks_->continueDecoding();
    return;
  }

  if (route_exists && require_route_match) {
    callbacks_->downstreamCallbacks()->refreshRouteConfigSnapshot();
    callbacks_->downstreamCallbacks()->clearRouteCache();
    route_exists = callbacks_->route().has_value();
    recordVirtualHost();
  }

  // Legacy behavior (guard disabled): recreate the stream so processing restarts from the
  // beginning against the newly-discovered route.
  // Track end_stream state to support stream recreation with fully read bodies.
  const bool can_recreate_stream = downstream_end_stream_;
  if (route_exists &&        // route can be resolved after an on-demand
                             // VHDS update
      can_recreate_stream && // Redirects require fully read body.
      callbacks_->recreateStream(/*headers=*/nullptr)) {
    return;
  }

  // route cannot be resolved after an on-demand VHDS update or
  // recreating stream failed, continue the filter-chain
  callbacks_->continueDecoding();
}

void OnDemandRouteUpdate::onClusterDiscoveryCompletion(
    Upstream::ClusterDiscoveryStatus cluster_status) {
  filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
  cluster_discovery_handle_.reset();
  finishClusterDiscovery(cluster_status);

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.on_demand_cluster_no_recreate_stream")) {
    // Whether or not the cluster exists, we continue decoding. Filters further down the
    // chain may want to weigh in on cluster selection, so we don't send a local reply
    // here.
    if (cluster_status == Upstream::ClusterDiscoveryStatus::Available) {
      callbacks_->downstreamCallbacks()->recreateClusterInfo();
    }
    callbacks_->continueDecoding();
    return;
  }

  // Track end_stream state to support stream recreation with fully read bodies.
  const bool can_recreate_stream = downstream_end_stream_;
  if (cluster_status == Upstream::ClusterDiscoveryStatus::Available && can_recreate_stream) {
    // Redirects require fully read body.
    const Http::ResponseHeaderMap* headers = nullptr;
    if (callbacks_->recreateStream(headers)) {
      callbacks_->downstreamCallbacks()->clearRouteCache();
      return;
    }
  }

  // Cluster still does not exist or we did not recreate the stream. Either way,
  // continue with the filter chain.
  if (cluster_status == Upstream::ClusterDiscoveryStatus::Available) {
    callbacks_->downstreamCallbacks()->recreateClusterInfo();
  }
  callbacks_->continueDecoding();
}

MonotonicTime OnDemandRouteUpdate::now() const {
  return callbacks_->dispatcher().timeSource().monotonicTime();
}

DiscoveryState& OnDemandRouteUpdate::discoveryState() {
  auto& filter_state = callbacks_->streamInfo().filterState();
  if (auto* state = filter_state->getDataMutable<DiscoveryState>(DiscoveryState::key());
      state != nullptr) {
    return *state;
  }
  auto state = std::make_shared<DiscoveryState>();
  auto& ref = *state;
  filter_state->setData(DiscoveryState::key(), std::move(state),
                        StreamInfo::FilterState::LifeSpan::Request);
  return ref;
}

namespace {
absl::string_view clusterStatusName(Upstream::ClusterDiscoveryStatus status) {
  switch (status) {
  case Upstream::ClusterDiscoveryStatus::Available:
    return DiscoveryState::Available;
  case Upstream::ClusterDiscoveryStatus::Missing:
    return DiscoveryState::Missing;
  case Upstream::ClusterDiscoveryStatus::Timeout:
    return DiscoveryState::Timeout;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

int64_t microseconds(MonotonicTime::duration duration) {
  return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

int64_t milliseconds(MonotonicTime::duration duration) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}
} // namespace

void OnDemandRouteUpdate::startRouteDiscovery(MonotonicTime start) {
  route_discovery_in_flight_ = true;
  route_discovery_start_ = start;
  auto& state = discoveryState();
  state.setRoute(DiscoveryState::Pending);
  if (request_headers_ != nullptr) {
    state.setHost(request_headers_->getHostValue());
  }
  if (auto stats = config_->stats(); stats.has_value()) {
    stats->route_discovery_requested_.inc();
    stats->route_discovery_pending_.inc();
  }
}

void OnDemandRouteUpdate::finishRouteDiscovery(bool route_exists) {
  if (!route_discovery_in_flight_) {
    return;
  }
  route_discovery_in_flight_ = false;
  const auto elapsed = now() - route_discovery_start_;
  discoveryState().setRoute(route_exists ? DiscoveryState::Found : DiscoveryState::NotFound,
                            microseconds(elapsed));
  if (auto stats = config_->stats(); stats.has_value()) {
    (route_exists ? stats->route_discovery_found_ : stats->route_discovery_not_found_).inc();
    stats->route_discovery_pending_.dec();
    // Only successful discoveries are timed, so that the histogram is the latency of a usable
    // answer rather than a mix with negative answers.
    if (route_exists) {
      stats->route_discovery_duration_.recordValue(milliseconds(elapsed));
    }
  }
}

void OnDemandRouteUpdate::recordVirtualHost() {
  // Only called after a route discovery completed, so the state exists. A route can only resolve
  // against the refreshed route config.
  if (auto route = callbacks_->route(); route.has_value()) {
    discoveryState().setVirtualHost(route->virtualHost().name());
  }
}

void OnDemandRouteUpdate::startClusterDiscovery(MonotonicTime start,
                                                absl::string_view cluster_name) {
  cluster_discovery_in_flight_ = true;
  cluster_discovery_start_ = start;
  auto& state = discoveryState();
  state.setCluster(DiscoveryState::Pending);
  state.setClusterName(cluster_name);
  if (auto stats = config_->stats(); stats.has_value()) {
    stats->cluster_discovery_requested_.inc();
    stats->cluster_discovery_pending_.inc();
  }
}

void OnDemandRouteUpdate::finishClusterDiscovery(Upstream::ClusterDiscoveryStatus cluster_status) {
  if (!cluster_discovery_in_flight_) {
    return;
  }
  cluster_discovery_in_flight_ = false;
  const auto elapsed = now() - cluster_discovery_start_;
  discoveryState().setCluster(clusterStatusName(cluster_status), microseconds(elapsed));
  if (auto stats = config_->stats(); stats.has_value()) {
    switch (cluster_status) {
    case Upstream::ClusterDiscoveryStatus::Available:
      stats->cluster_discovery_available_.inc();
      // Only successful discoveries are timed: a timeout always lasts about the configured
      // timeout, and a missing cluster is a quick negative answer, so neither says anything about
      // how long it takes to get a usable cluster.
      stats->cluster_discovery_duration_.recordValue(milliseconds(elapsed));
      break;
    case Upstream::ClusterDiscoveryStatus::Missing:
      stats->cluster_discovery_missing_.inc();
      break;
    case Upstream::ClusterDiscoveryStatus::Timeout:
      stats->cluster_discovery_timeout_.inc();
      break;
    }
    stats->cluster_discovery_pending_.dec();
  }
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
