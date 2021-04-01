#include "extensions/filters/http/on_demand/on_demand_update.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/http/codes.h"
#include "common/upstream/odcds_api_impl.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {

namespace {

template <typename ProtoConfig>
Upstream::OdCdsApiSharedPtr createOdCdsApi(const ProtoConfig& proto_config,
                                           Upstream::ClusterManager& cm, Stats::Scope& scope,
                                           ProtobufMessage::ValidationVisitor& validation_visitor) {
  if (!proto_config.has_odcds_config()) {
    return nullptr;
  }
  return Upstream::OdCdsApiImpl::create(proto_config.odcds_config(), cm, scope, validation_visitor);
}

} // namespace

OnDemandFilterConfig::OnDemandFilterConfig(Upstream::ClusterManager& cm,
                                           Upstream::OdCdsApiSharedPtr odcds)
    : cm_(cm), odcds_(std::move(odcds)) {}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
    Upstream::ClusterManager& cm, Stats::Scope& scope,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : OnDemandFilterConfig(cm, createOdCdsApi(proto_config, cm, scope, validation_visitor)) {}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
    Upstream::ClusterManager& cm, Stats::Scope& scope,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : OnDemandFilterConfig(cm, createOdCdsApi(proto_config, cm, scope, validation_visitor)) {}

Http::FilterHeadersStatus OnDemandRouteUpdate::decodeHeaders(Http::RequestHeaderMap&, bool) {

  if (auto route = callbacks_->route(); route != nullptr) {
    handleOnDemandCDS(*route);
    return filter_iteration_state_;
  }
  // decodeHeaders() is interrupted.
  decode_headers_active_ = true;
  route_config_updated_callback_ =
      std::make_shared<Http::RouteConfigUpdatedCallback>(Http::RouteConfigUpdatedCallback(
          [this](bool route_exists) -> void { onRouteConfigUpdateCompletion(route_exists); }));
  filter_iteration_state_ = Http::FilterHeadersStatus::StopIteration;
  callbacks_->requestRouteConfigUpdate(route_config_updated_callback_);
  // decodeHeaders() is completed.
  decode_headers_active_ = false;
  return filter_iteration_state_;
}

void OnDemandRouteUpdate::handleOnDemandCDS(const Router::Route& route) {
  auto entry = route.routeEntry();
  if (entry == nullptr) {
    // No entry? Nothing we can do here.
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  auto config = getConfig(*entry);
  if (config == nullptr) {
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  auto odcds = config->odcds();
  if (odcds == nullptr) {
    // No ODCDS? It means that on-demand discovery is disabled.
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  auto& cm = config->clusterManager();
  if (callbacks_->clusterInfo() != nullptr) {
    // Cluster already exists, so nothing to do here.
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  auto cluster_name = entry->clusterName();
  if (cluster_name.empty()) {
    // Empty cluster name may be a result of a missing HTTP header
    // used for getting the cluster name. Nothing we can do here.
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return;
  }
  filter_iteration_state_ = Http::FilterHeadersStatus::StopIteration;
  cluster_discovery_callback_ = std::make_shared<Upstream::ClusterDiscoveryCallback>(
      [this](Upstream::ClusterDiscoveryStatus cluster_status) {
        onClusterDiscoveryCompletion(cluster_status);
      });
  cluster_discovery_handle_ =
      cm.requestOnDemandClusterDiscovery(odcds, cluster_name, cluster_discovery_callback_);
}

const OnDemandFilterConfig* OnDemandRouteUpdate::getConfig(const Router::RouteEntry& entry) {
  auto config =
      entry.mostSpecificPerFilterConfigTyped<OnDemandFilterConfig>(HttpFilterNames::get().OnDemand);
  if (config != nullptr) {
    return config;
  }
  return config_.get();
}

Http::FilterDataStatus OnDemandRouteUpdate::decodeData(Buffer::Instance&, bool) {
  return filter_iteration_state_ == Http::FilterHeadersStatus::StopIteration
             ? Http::FilterDataStatus::StopIterationAndWatermark
             : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus OnDemandRouteUpdate::decodeTrailers(Http::RequestTrailerMap&) {
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
  cluster_discovery_callback_.reset();
  cluster_discovery_handle_.reset();
}

// This is the callback which is called when an update requested in requestRouteConfigUpdate()
// has been propagated to workers, at which point the request processing is restarted from the
// beginning.
void OnDemandRouteUpdate::onRouteConfigUpdateCompletion(bool route_exists) {
  filter_iteration_state_ = Http::FilterHeadersStatus::Continue;

  // Don't call continueDecoding in the middle of decodeHeaders()
  if (decode_headers_active_) {
    return;
  }

  if (route_exists &&                  // route can be resolved after an on-demand
                                       // VHDS update
      !callbacks_->decodingBuffer() && // Redirects with body not yet supported.
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
  cluster_discovery_callback_.reset();
  cluster_discovery_handle_.reset();
  if (cluster_status == Upstream::ClusterDiscoveryStatus::Available &&
      !callbacks_->decodingBuffer()) { // Redirects with body not yet supported.
    const Http::ResponseHeaderMap* headers = nullptr;
    if (callbacks_->recreateStream(headers)) {
      callbacks_->clearRouteCache();
      return;
    }
  }

  // Cluster still does not exist or we failed to recreate the
  // stream. Either way, continue with the filter-chain.
  callbacks_->continueDecoding();
}

} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
