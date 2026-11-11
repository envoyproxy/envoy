#include "source/extensions/filters/http/on_demand/on_demand_update.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/config/xds_resource.h"
#include "source/common/http/codes.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/upstream/od_cds_api_impl.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "absl/container/inlined_vector.h"
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
  RdsCdsDecodeHeadersBehavior(Upstream::OdCdsApiHandlePtr odcds, std::chrono::milliseconds timeout,
                              std::optional<ShadowDiscoveryOptions> shadow_discovery)
      : odcds_(std::move(odcds)), timeout_(timeout), shadow_discovery_(shadow_discovery) {}

  void decodeHeaders(OnDemandRouteUpdate& filter) override {
    auto route = filter.handleMissingRoute();
    if (!route.has_value()) {
      return;
    }
    filter.handleOnDemandCds(route.value(), *odcds_, timeout_);
    // Only when the request is not paused: if it is, onClusterDiscovered() or the re-run of this
    // method will start the shadow discovery once the request's own cluster is known.
    if (shadow_discovery_.has_value() &&
        filter.filterIterationState() == Http::FilterHeadersStatus::Continue) {
      filter.handleShadowDiscovery(*odcds_, timeout_, *shadow_discovery_);
    }
  }

  void onClusterDiscovered(OnDemandRouteUpdate& filter) override {
    if (shadow_discovery_.has_value()) {
      filter.handleShadowDiscovery(*odcds_, timeout_, *shadow_discovery_);
    }
  }

private:
  Upstream::OdCdsApiHandlePtr odcds_;
  std::chrono::milliseconds timeout_;
  std::optional<ShadowDiscoveryOptions> shadow_discovery_;
};

} // namespace

DecodeHeadersBehaviorPtr DecodeHeadersBehavior::rds() {
  return std::make_unique<RdsDecodeHeadersBehavior>();
}

DecodeHeadersBehaviorPtr
DecodeHeadersBehavior::cdsRds(Upstream::OdCdsApiHandlePtr odcds, std::chrono::milliseconds timeout,
                              std::optional<ShadowDiscoveryOptions> shadow_discovery) {
  return std::make_unique<RdsCdsDecodeHeadersBehavior>(std::move(odcds), timeout, shadow_discovery);
}

namespace {

absl::StatusOr<DecodeHeadersBehaviorPtr> createDecodeHeadersBehavior(
    OptRef<const envoy::extensions::filters::http::on_demand::v3::OnDemandCds> odcds_config,
    Upstream::ClusterManager& cm, Runtime::Loader& runtime,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
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
  std::optional<ShadowDiscoveryOptions> shadow_discovery;
  if (odcds_config->has_shadow_cluster_discovery()) {
    shadow_discovery = ShadowDiscoveryOptions{cm, runtime};
  }
  return DecodeHeadersBehavior::cdsRds(std::move(odcds), timeout, shadow_discovery);
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
    Upstream::ClusterManager& cm, Runtime::Loader& runtime,
    ProtobufMessage::ValidationVisitor& validation_visitor, absl::Status& creation_status) {
  auto behavior_or =
      createDecodeHeadersBehavior(getOdCdsConfig(proto_config), cm, runtime, validation_visitor);
  SET_AND_RETURN_IF_NOT_OK(behavior_or.status(), creation_status);
  behavior_ = std::move(behavior_or.value());
}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
    Upstream::ClusterManager& cm, Runtime::Loader& runtime,
    ProtobufMessage::ValidationVisitor& validation_visitor, absl::Status& creation_status,
    Stats::Scope& scope, const std::string& stats_prefix)
    : OnDemandFilterConfig(proto_config, cm, runtime, validation_visitor, creation_status) {
  stats_.emplace(generateStats(scope, stats_prefix));
}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
    Upstream::ClusterManager& cm, Runtime::Loader& runtime,
    ProtobufMessage::ValidationVisitor& validation_visitor, absl::Status& creation_status) {
  auto behavior_or =
      createDecodeHeadersBehavior(getOdCdsConfig(proto_config), cm, runtime, validation_visitor);
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
  // A callback that already ran means there was nothing to discover.
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

void OnDemandRouteUpdate::handleShadowDiscovery(Upstream::OdCdsApiHandle& odcds,
                                                std::chrono::milliseconds timeout,
                                                const ShadowDiscoveryOptions& options) {
  // This is best-effort and must never interfere with the request: it does not touch
  // filter_iteration_state_, the discovery state, or the stats of the request's own cluster.
  if (shadow_discovery_started_) {
    return;
  }
  const auto route = callbacks_->route();
  const auto headers = callbacks_->requestHeaders();
  if (!route.has_value() || !headers.has_value() || route->routeEntry() == nullptr ||
      headers->getMethodValue() == Http::Headers::get().MethodValues.Connect) {
    // No route, or a CONNECT request, which the router does not mirror.
    return;
  }
  shadow_discovery_started_ = true;

  // Pick the policies like the router does: cluster-level policies win over route-level ones.
  const std::vector<Router::ShadowPolicyPtr>* policies = &route->routeEntry()->shadowPolicies();
  if (const auto cluster_info = callbacks_->clusterInfo(); cluster_info.has_value()) {
    const auto& cluster_policies = cluster_info->httpProtocolOptions().shadowPolicies();
    if (!cluster_policies.empty()) {
      policies = &cluster_policies;
    }
  }

  absl::InlinedVector<absl::string_view, 2> requested;
  for (const auto& policy : *policies) {
    // Policies that select their cluster with a header are not discovered: the name is controlled
    // by the client.
    const absl::string_view cluster_name = policy->cluster();
    if (cluster_name.empty()) {
      continue;
    }
    // Only for the requests the router will mirror, using the same random as the router.
    if (!options.runtime_.snapshot().featureEnabled(policy->runtimeKey(), policy->defaultValue(),
                                                    callbacks_->streamId())) {
      continue;
    }
    if (std::find(requested.begin(), requested.end(), cluster_name) != requested.end() ||
        options.cluster_manager_.getThreadLocalCluster(cluster_name) != nullptr) {
      continue;
    }
    requested.push_back(cluster_name);

    ++shadow_discovery_in_flight_;
    if (auto stats = config_->stats(); stats.has_value()) {
      stats->shadow_cluster_discovery_requested_.inc();
      stats->shadow_cluster_discovery_pending_.inc();
    }
    auto callback = std::make_unique<Upstream::ClusterDiscoveryCallback>(
        [this](Upstream::ClusterDiscoveryStatus cluster_status) {
          finishShadowClusterDiscovery(cluster_status);
        });
    // The handle only cancels the callback: if the stream ends first, the discovery still
    // completes and the cluster is available to later requests.
    shadow_discovery_handles_.push_back(
        odcds.requestOnDemandClusterDiscovery(cluster_name, std::move(callback), timeout));
  }
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
  shadow_discovery_handles_.clear();
}

// Runs before the access logs (onDestroy() does not), so they can show a cancelled discovery.
void OnDemandRouteUpdate::onStreamComplete() {
  // Still in flight: the stream ended before the discovery did.
  if (route_discovery_in_flight_) {
    discoveryState().routeCancelled();
  }
  if (cluster_discovery_in_flight_) {
    discoveryState().clusterCancelled();
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
    if (shadow_discovery_in_flight_ > 0) {
      stats->shadow_cluster_discovery_cancelled_.add(shadow_discovery_in_flight_);
      stats->shadow_cluster_discovery_pending_.sub(shadow_discovery_in_flight_);
    }
  }
  route_discovery_in_flight_ = false;
  cluster_discovery_in_flight_ = false;
  shadow_discovery_in_flight_ = 0;
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

  // Whether or not the cluster exists, we continue decoding. Filters further down the
  // chain may want to weigh in on cluster selection, so we don't send a local reply
  // here.
  if (cluster_status == Upstream::ClusterDiscoveryStatus::Available) {
    callbacks_->downstreamCallbacks()->recreateClusterInfo();
    // Cluster-level mirror policies are only known now. This does not delay the request.
    getConfig()->decodeHeadersBehavior().onClusterDiscovered(*this);
  }
  callbacks_->continueDecoding();
}

MonotonicTime OnDemandRouteUpdate::now() const {
  return callbacks_->dispatcher().timeSource().monotonicTime();
}

DiscoveryState& OnDemandRouteUpdate::discoveryState() {
  if (discovery_state_ != nullptr) {
    return *discovery_state_;
  }
  // Request scoped, so a recreated stream finds the state of the original one.
  auto& filter_state = callbacks_->streamInfo().filterState();
  discovery_state_ = std::dynamic_pointer_cast<DiscoveryState>(
      filter_state->getDataSharedMutableGeneric(DiscoveryState::key()));
  if (discovery_state_ == nullptr) {
    discovery_state_ = std::make_shared<DiscoveryState>();
    filter_state->setData(DiscoveryState::key(), discovery_state_,
                          StreamInfo::FilterState::LifeSpan::Request);
  }
  return *discovery_state_;
}

namespace {
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
  state.routePending();
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
  discoveryState().routeFinished(route_exists, microseconds(elapsed));
  if (auto stats = config_->stats(); stats.has_value()) {
    (route_exists ? stats->route_discovery_found_ : stats->route_discovery_not_found_).inc();
    stats->route_discovery_pending_.dec();
    // Only successful discoveries are timed; negative answers would skew the latency.
    if (route_exists) {
      stats->route_discovery_duration_.recordValue(milliseconds(elapsed));
    }
  }
}

void OnDemandRouteUpdate::recordVirtualHost() {
  // Called after a route discovery completed, so the state exists.
  if (auto route = callbacks_->route(); route.has_value()) {
    discoveryState().setVirtualHost(route->virtualHost().name());
  }
}

void OnDemandRouteUpdate::startClusterDiscovery(MonotonicTime start,
                                                absl::string_view cluster_name) {
  cluster_discovery_in_flight_ = true;
  cluster_discovery_start_ = start;
  auto& state = discoveryState();
  state.clusterPending();
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
  discoveryState().clusterFinished(cluster_status, microseconds(elapsed));
  if (auto stats = config_->stats(); stats.has_value()) {
    switch (cluster_status) {
    case Upstream::ClusterDiscoveryStatus::Available:
      stats->cluster_discovery_available_.inc();
      // Only Available is timed: a timeout lasts about the configured timeout, and Missing is a
      // quick negative answer.
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

void OnDemandRouteUpdate::finishShadowClusterDiscovery(
    Upstream::ClusterDiscoveryStatus cluster_status) {
  if (shadow_discovery_in_flight_ == 0) {
    // The stream is complete and already accounted for these as cancelled.
    return;
  }
  --shadow_discovery_in_flight_;
  if (auto stats = config_->stats(); stats.has_value()) {
    switch (cluster_status) {
    case Upstream::ClusterDiscoveryStatus::Available:
      stats->shadow_cluster_discovery_available_.inc();
      break;
    case Upstream::ClusterDiscoveryStatus::Missing:
      stats->shadow_cluster_discovery_missing_.inc();
      break;
    case Upstream::ClusterDiscoveryStatus::Timeout:
      stats->shadow_cluster_discovery_timeout_.inc();
      break;
    }
    stats->shadow_cluster_discovery_pending_.dec();
  }
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
