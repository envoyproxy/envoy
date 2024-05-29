#include "source/extensions/filters/http/on_demand/on_demand_update.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/config/xds_resource.h"
#include "source/common/http/codes.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/well_known_names.h"

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

DecodeHeadersBehaviorPtr createDecodeHeadersBehavior(
    OptRef<const envoy::extensions::filters::http::on_demand::v3::OnDemandCds> odcds_config,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor) {
  if (!odcds_config.has_value()) {
    return DecodeHeadersBehavior::rds();
  }
  Upstream::OdCdsApiHandlePtr odcds;
  if (odcds_config->resources_locator().empty()) {
    odcds = cm.allocateOdCdsApi(odcds_config->source(), absl::nullopt, validation_visitor);
  } else {
    auto locator = THROW_OR_RETURN_VALUE(
        Config::XdsResourceIdentifier::decodeUrl(odcds_config->resources_locator()),
        xds::core::v3::ResourceLocator);
    odcds = cm.allocateOdCdsApi(odcds_config->source(), locator, validation_visitor);
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
    return absl::nullopt;
  }
  return proto_config.odcds();
}

} // namespace

OnDemandFilterConfig::OnDemandFilterConfig(DecodeHeadersBehaviorPtr behavior)
    : behavior_(std::move(behavior)) {}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::OnDemand& proto_config,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor)
    : OnDemandFilterConfig(
          createDecodeHeadersBehavior(getOdCdsConfig(proto_config), cm, validation_visitor)) {}

OnDemandFilterConfig::OnDemandFilterConfig(
    const envoy::extensions::filters::http::on_demand::v3::PerRouteConfig& proto_config,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor)
    : OnDemandFilterConfig(
          createDecodeHeadersBehavior(getOdCdsConfig(proto_config), cm, validation_visitor)) {}

OnDemandRouteUpdate::OnDemandRouteUpdate(OnDemandFilterConfigSharedPtr config)
    : config_(std::move(config)) {
  if (config_ == nullptr) {
    // if config is nil, fall back to rds only
    config_ = std::make_shared<OnDemandFilterConfig>(DecodeHeadersBehavior::rds());
  }
}

OptRef<const Router::Route> OnDemandRouteUpdate::handleMissingRoute() {
  if (auto route = callbacks_->route(); route != nullptr) {
    filter_iteration_state_ = Http::FilterHeadersStatus::Continue;
    return *route;
  }
  // decodeHeaders() is interrupted.
  decode_headers_active_ = true;
  route_config_updated_callback_ =
      std::make_shared<Http::RouteConfigUpdatedCallback>(Http::RouteConfigUpdatedCallback(
          [this](bool route_exists) -> void { onRouteConfigUpdateCompletion(route_exists); }));
  filter_iteration_state_ = Http::FilterHeadersStatus::StopIteration;
  callbacks_->downstreamCallbacks()->requestRouteConfigUpdate(route_config_updated_callback_);
  // decodeHeaders() is completed.
  decode_headers_active_ = false;
  return makeOptRefFromPtr(callbacks_->route().get());
}

Http::FilterHeadersStatus OnDemandRouteUpdate::decodeHeaders(Http::RequestHeaderMap&, bool) {
  auto config = getConfig();

  config->decodeHeadersBehavior().decodeHeaders(*this);
  return filter_iteration_state_;
}

// Passed route pointer here is not null.
void OnDemandRouteUpdate::handleOnDemandCds(const Router::Route& route,
                                            Upstream::OdCdsApiHandle& odcds,
                                            std::chrono::milliseconds timeout) {
  if (callbacks_->clusterInfo() != nullptr) {
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
  cluster_discovery_handle_.reset();
  if (cluster_status == Upstream::ClusterDiscoveryStatus::Available &&
      !callbacks_->decodingBuffer()) { // Redirects with body not yet supported.
    const Http::ResponseHeaderMap* headers = nullptr;
    if (callbacks_->recreateStream(headers)) {
      callbacks_->downstreamCallbacks()->clearRouteCache();
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
