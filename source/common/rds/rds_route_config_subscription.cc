#include "source/common/rds/rds_route_config_subscription.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/rds/util.h"

namespace Envoy {
namespace Rds {

absl::StatusOr<std::unique_ptr<RdsRouteConfigSubscription>> RdsRouteConfigSubscription::create(
    RouteConfigUpdatePtr&& config_update,
    Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& route_config_name, const uint64_t manager_identifier,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    const std::string& rds_type, RouteConfigProviderManager& route_config_provider_manager) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<RdsRouteConfigSubscription>(new RdsRouteConfigSubscription(
      std::move(config_update), std::move(resource_decoder), config_source, route_config_name,
      manager_identifier, factory_context, stat_prefix, rds_type, route_config_provider_manager,
      creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

RdsRouteConfigSubscription::RdsRouteConfigSubscription(
    RouteConfigUpdatePtr&& config_update,
    Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& route_config_name, const uint64_t manager_identifier,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    const std::string& rds_type, RouteConfigProviderManager& route_config_provider_manager,
    absl::Status& creation_status)
    : route_config_name_(route_config_name),
      scope_(factory_context.scope().createScope(stat_prefix + route_config_name_ + ".")),
      factory_context_(factory_context),
      parent_init_target_(
          fmt::format("RdsRouteConfigSubscription {} init {}", rds_type, route_config_name_),
          [this]() { local_init_manager_.initialize(local_init_watcher_); }),
      local_init_watcher_(fmt::format("{} local-init-watcher {}", rds_type, route_config_name_),
                          [this]() { parent_init_target_.ready(); }),
      local_init_target_(fmt::format("RdsRouteConfigSubscription {} local-init-target {}", rds_type,
                                     route_config_name_),
                         [this]() { subscription_->start({route_config_name_}); }),
      local_init_manager_(fmt::format("{} local-init-manager {}", rds_type, route_config_name_)),
      stat_prefix_(stat_prefix), rds_type_(rds_type),
      stats_({ALL_RDS_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))}),
      route_config_provider_manager_(route_config_provider_manager),
      manager_identifier_(manager_identifier), config_update_info_(std::move(config_update)),
      resource_decoder_(std::move(resource_decoder)) {
  const auto resource_type = route_config_provider_manager_.protoTraits().resourceType();
  auto subscription_or_error =
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.xdstp_based_config_singleton_subscriptions")
          ? factory_context.xdsManager().subscribeToSingletonResource(
                route_config_name_, config_source, Envoy::Grpc::Common::typeUrl(resource_type),
                *scope_, *this, resource_decoder_, {})
          : factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
                config_source, Envoy::Grpc::Common::typeUrl(resource_type), *scope_, *this,
                resource_decoder_, {});
  SET_AND_RETURN_IF_NOT_OK(subscription_or_error.status(), creation_status);
  subscription_ = std::move(*subscription_or_error);
  local_init_manager_.add(local_init_target_);
  // The receiver warms up the route configuration that it builds on every update and notifies this
  // subscription once it's ready to be published.
  config_update_info_->setObserver(*this);
}

RdsRouteConfigSubscription::~RdsRouteConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  local_init_target_.ready();

  // The ownership of RdsRouteConfigProviderImpl is shared among all HttpConnectionManagers that
  // hold a shared_ptr to it. The RouteConfigProviderManager holds weak_ptrs to the
  // RdsRouteConfigProviders. Therefore, the map entry for the RdsRouteConfigProvider has to get
  // cleaned by the RdsRouteConfigProvider's destructor.
  route_config_provider_manager_.eraseDynamicProvider(manager_identifier_);
}

absl::Status RdsRouteConfigSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& resources,
    const std::string& version_info) {
  if (resources.empty()) {
    ENVOY_LOG(debug, "Missing {} RouteConfiguration for {} in onConfigUpdate()", rds_type_,
              route_config_name_);
    stats_.update_empty_.inc();
    // Don't signal readiness if a previous update is still warming up, that update publishes and
    // signals readiness itself. An empty resource list doesn't invalidate it: it leaves the
    // currently published route configuration in place.
    if (!config_update_info_->configWarming()) {
      local_init_target_.ready();
    }
    return absl::OkStatus();
  }
  if (resources.size() != 1) {
    const auto msg = fmt::format("Unexpected {} resource length: {}", rds_type_, resources.size());
    ENVOY_LOG(warn, "rds: route config '{}' rejected: {}", route_config_name_, msg);
    return absl::InvalidArgumentError(msg);
  }

  const auto& route_config = resources[0].get().resource();
  Protobuf::ReflectableMessage reflectable_config = createReflectableMessage(route_config);
  if (reflectable_config->GetDescriptor()->full_name() !=
      route_config_provider_manager_.protoTraits().resourceType()) {
    const auto msg = fmt::format("Unexpected {} configuration type (expecting {}): {}", rds_type_,
                                 route_config_provider_manager_.protoTraits().resourceType(),
                                 reflectable_config->GetDescriptor()->full_name());
    ENVOY_LOG(warn, "rds: route config '{}' rejected: {}", route_config_name_, msg);
    return absl::InvalidArgumentError(msg);
  }
  if (resourceName(route_config_provider_manager_.protoTraits(), route_config) !=
      route_config_name_) {
    const auto msg =
        fmt::format("Unexpected {} configuration (expecting {}): {}", rds_type_, route_config_name_,
                    resourceName(route_config_provider_manager_.protoTraits(), route_config));
    ENVOY_LOG(warn, "rds: route config '{}' rejected: {}", route_config_name_, msg);
    return absl::InvalidArgumentError(msg);
  }
  // The new route configuration is built and warmed up by the receiver, which calls back into
  // onConfigWarmed() to publish it. Note that this may happen before onRdsUpdate() returns, i.e.
  // synchronously, if there is nothing to warm up, so the publishing state is reset upfront.
  publish_status_ = absl::OkStatus();
  RETURN_IF_NOT_OK(config_update_info_->onRdsUpdate(route_config, version_info));
  RETURN_IF_NOT_OK_REF(publish_status_);

  // If the update was applied and there was nothing to warm up, onConfigWarmed() has already run
  // and signalled readiness, so this is a no-op. Otherwise signal it here, unless an update is
  // still warming up - that one publishes and signals readiness itself - or unless publishing
  // failed, in which case nothing should be told that a route configuration is ready.
  if (!config_update_info_->configWarming()) {
    local_init_target_.ready();
  }

  // If there was nothing to warm up, the update was published before we got here, so a failure can
  // still be reported to the xDS layer as a rejection. Otherwise the publishing happens later and
  // publish_status_ is still OK here.
  return publish_status_;
}

void RdsRouteConfigSubscription::onConfigWarmed() {
  Cleanup after_this_update([this]() {
    // Only signal readiness if the new route configuration actually went live, so that whatever
    // warms up with this subscription isn't told that a route configuration is ready when it
    // isn't.
    //
    // If the publishing happened synchronously, i.e. if onConfigUpdate() is still on the stack,
    // the failure is returned to the xDS layer, which rejects the update and calls
    // onConfigUpdateFailed(). That signals readiness, so server startup isn't blocked by a bad
    // config. If the publishing happened asynchronously the update has already been accepted, so
    // there is no such rejection: this subscription stays unready and whatever warms up with it,
    // e.g. a listener, stays warming. The warning logged below is the only indication of that.
    if (publish_status_.ok()) {
      local_init_target_.ready();
    } else {
      ENVOY_LOG(warn, "rds: failed to apply the warmed up route config '{}': {}",
                route_config_name_, publish_status_.message());
    }
  });

  stats_.config_reload_.inc();
  stats_.config_reload_time_ms_.set(DateUtil::nowToMilliseconds(factory_context_.timeSource()));

  publish_status_ = beforeProviderUpdate();
  RETURN_ONLY_IF_NOT_OK_REF(publish_status_);

  ENVOY_LOG(debug, "rds: loading new configuration: config_name={} hash={}", route_config_name_,
            config_update_info_->configHash());

  if (route_config_provider_ != nullptr) {
    publish_status_ = route_config_provider_->onConfigUpdate();
    RETURN_ONLY_IF_NOT_OK_REF(publish_status_);
  }

  publish_status_ = afterProviderUpdate();
}

absl::Status RdsRouteConfigSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    // TODO(#2500) when on-demand resource loading is supported, an RDS removal may make sense
    // (see discussion in #6879), and so we should do something other than ignoring here.
    ENVOY_LOG(trace,
              "Server sent a delta {} update attempting to remove a resource (name: {}). Ignoring.",
              rds_type_, removed_resources[0]);
  }
  if (!added_resources.empty()) {
    return onConfigUpdate(added_resources, added_resources[0].get().version());
  }
  return absl::OkStatus();
}

void RdsRouteConfigSubscription::onConfigUpdateFailed(
    Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  local_init_target_.ready();
}

} // namespace Rds
} // namespace Envoy
