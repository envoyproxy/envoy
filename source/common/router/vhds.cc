#include "source/common/router/vhds.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

absl::StatusOr<VhdsSubscriptionPtr> VhdsSubscription::createVhdsSubscription(
    RouteConfigUpdatePtr& config_update_info,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Rds::RouteConfigProvider* route_config_provider) {
  const auto& vhds_config_source =
      config_update_info->protobufConfigurationCast().vhds().config_source();
  // VHDS only supports Delta xDS. This can be specified either explicitly via DELTA_GRPC
  // or implicitly by using ADS when the parent ADS stream is in Delta mode.
  const bool is_ads = vhds_config_source.config_source_specifier_case() ==
                      envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds;
  const bool is_delta_grpc = vhds_config_source.has_api_config_source() &&
                             vhds_config_source.api_config_source().api_type() ==
                                 envoy::config::core::v3::ApiConfigSource::DELTA_GRPC;

  if (!is_ads && !is_delta_grpc) {
    return absl::InvalidArgumentError(
        "vhds: only 'DELTA_GRPC' or 'ADS' (which uses Delta xDS) is supported as a config source.");
  }

  // If using ADS, verify the parent ADS stream is in Delta mode
  if (is_ads) {
    const auto& bootstrap = factory_context.bootstrap();
    if (!bootstrap.has_dynamic_resources() || !bootstrap.dynamic_resources().has_ads_config()) {
      return absl::InvalidArgumentError(
          "vhds: ADS config source specified but no ADS configured in bootstrap.");
    }
    const auto& ads_config = bootstrap.dynamic_resources().ads_config();
    if (ads_config.api_type() != envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
      return absl::InvalidArgumentError(
          "vhds: ADS must use DELTA_GRPC api_type when used as VHDS config source.");
    }
  }

  auto status = absl::OkStatus();
  auto ret = std::unique_ptr<VhdsSubscription>(new VhdsSubscription(
      config_update_info, factory_context, stat_prefix, route_config_provider, status));
  RETURN_IF_ERROR(status);
  return ret;
}

// Implements callbacks to handle DeltaDiscovery protocol for VirtualHostDiscoveryService
VhdsSubscription::VhdsSubscription(RouteConfigUpdatePtr& config_update_info,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   const std::string& stat_prefix,
                                   Rds::RouteConfigProvider* route_config_provider,
                                   absl::Status& status)
    : config_update_info_(config_update_info),
      scope_(factory_context.scope().createScope(
          stat_prefix + "vhds." + config_update_info_->protobufConfigurationCast().name() + ".")),
      stats_({ALL_VHDS_STATS(POOL_COUNTER(*scope_))}),
      init_target_(fmt::format("VhdsConfigSubscription {}",
                               config_update_info_->protobufConfigurationCast().name()),
                   [this]() {
                     subscription_->start(
                         {config_update_info_->protobufConfigurationCast().name()});
                   }),
      resource_type_helper_(factory_context.messageValidationContext().dynamicValidationVisitor(),
                            "name"),
      route_config_provider_(route_config_provider) {
  const auto resource_name = resource_type_helper_.getResourceName();
  Envoy::Config::SubscriptionOptions options;
  options.use_namespace_matching_ = true;
  absl::StatusOr<Envoy::Config::SubscriptionPtr> status_or =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_update_info_->protobufConfigurationCast().vhds().config_source(),
          Grpc::Common::typeUrl(resource_name), *scope_, *this,
          resource_type_helper_.resourceDecoder(), options);
  SET_AND_RETURN_IF_NOT_OK(status_or.status(), status);
  subscription_ = std::move(status_or.value());
}

void VhdsSubscription::updateOnDemand(const std::string& with_route_config_name_prefix) {
  subscription_->requestOnDemandUpdate({with_route_config_name_prefix});
}

void VhdsSubscription::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                            const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

void VhdsSubscription::commitUpdateInitManager(
    std::unique_ptr<Init::ManagerImpl> update_init_manager, absl::string_view version_info) {
  if (update_init_manager_ != nullptr) {
    // A previous update is still warming up. This update supersedes it: the route configuration it
    // would have published has already been replaced in config_update_info_, so drop its watcher
    // and init manager and never publish it.
    ENVOY_LOG(debug,
              "vhds: route config '{}' was updated again while the previous update was still "
              "warming up, abandoning the previous update",
              config_update_info_->protobufConfigurationCast().name());
  }
  // Assigning the watcher first drops the abandoned watcher while its init manager is still around.
  // That is safe, the manager only holds a weak handle to it.
  update_init_watcher_ = std::make_unique<Init::WatcherImpl>(
      fmt::format("VHDS update-init-watcher {}:{}",
                  config_update_info_->protobufConfigurationCast().name(), version_info),
      [this]() { onUpdateInitManagerReady(); });
  update_init_manager_ = std::move(update_init_manager);
  // Note this publishes the update synchronously, i.e. before returning, if there is nothing to
  // warm up. It may therefore reset the two members that were just assigned.
  update_init_manager_->initialize(*update_init_watcher_);
}

void VhdsSubscription::resetUpdateInitManager() {
  // Note this is normally called from inside the readiness callback of update_init_manager_ itself.
  // That is safe: the callback is invoked through a handle that holds a shared_ptr to the callback
  // for the duration of the call, and neither the manager nor the watcher touches its own state
  // after invoking it.
  update_init_watcher_.reset();
  update_init_manager_.reset();
}

absl::Status VhdsSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  RouteConfigUpdateReceiver::VirtualHostRefVector added_vhosts;
  std::set<std::string> added_resource_ids;
  for (const auto& resource : added_resources) {
    added_resource_ids.emplace(resource.get().name());
    std::copy(resource.get().aliases().begin(), resource.get().aliases().end(),
              std::inserter(added_resource_ids, added_resource_ids.end()));
    // the management server returns empty resources (they contain no virtual hosts in this case)
    // for aliases that it couldn't resolve.
    if (!resource.get().hasResource()) {
      continue;
    }
    added_vhosts.emplace_back(
        Envoy::Protobuf::DynamicCastMessage<envoy::config::route::v3::VirtualHost>(
            resource.get().resource()));
  }
  // Every VHDS update gets its own independent init manager, so that the resources of the new route
  // configuration are warmed up without interfering with the route configuration that is currently
  // published. It is kept local until the update is known to be applied, so that an update that
  // turns out to be a no-op leaves a previous update that is still warming up alone.
  auto update_init_manager = std::make_unique<Init::ManagerImpl>(
      fmt::format("VHDS update-init-manager {}:{}",
                  config_update_info_->protobufConfigurationCast().name(), version_info));
  if (!config_update_info_->onVhdsUpdate(added_vhosts, std::move(added_resource_ids),
                                         removed_resources, *update_init_manager, version_info)) {
    // The route configuration is unchanged, so there is nothing to warm up and nothing to publish.
    // Note that update_init_manager is dropped here without ever being started, while a previous
    // update that is still warming up is deliberately left untouched.
    if (update_init_manager_ == nullptr) {
      init_target_.ready();
    }
    // Otherwise readiness is signalled once the update that is still warming up is published.
    return absl::OkStatus();
  }

  // The new route configuration has been built but is not visible to the workers yet. Wait until
  // everything that registered to the per-update init manager is warmed up, and only publish the
  // new route configuration then.
  publish_status_ = absl::OkStatus();
  commitUpdateInitManager(std::move(update_init_manager), version_info);

  // If there was nothing to warm up, the watcher registered above has already run and the update
  // was published before we got here, so a failure can still be reported to the xDS layer as a
  // rejection. Otherwise the publishing happens later and publish_status_ is still OK here.
  return publish_status_;
}

void VhdsSubscription::onUpdateInitManagerReady() {
  stats_.config_reload_.inc();
  ENVOY_LOG(debug, "vhds: loading new configuration: config_name={} hash={}",
            config_update_info_->protobufConfigurationCast().name(),
            config_update_info_->configHash());
  if (route_config_provider_ != nullptr) {
    publish_status_ = route_config_provider_->onConfigUpdate();
    if (!publish_status_.ok()) {
      ENVOY_LOG(warn, "vhds: failed to apply the warmed up route config '{}': {}",
                config_update_info_->protobufConfigurationCast().name(), publish_status_.message());
    }
  }

  // The new route configuration is warmed up and published, so the per-update init manager isn't
  // needed anymore. The next update will create a new one.
  resetUpdateInitManager();

  // Only signal readiness if the new route configuration actually went live, so that whatever
  // warms up with this subscription isn't told that a route configuration is ready when it isn't.
  //
  // If the publishing happened synchronously, i.e. if onConfigUpdate() is still on the stack, the
  // failure is returned to the xDS layer, which rejects the update and calls
  // onConfigUpdateFailed(). That signals readiness, so server startup isn't blocked by a bad
  // config. If the publishing happened asynchronously the update has already been accepted, so
  // there is no such rejection: this subscription stays unready and whatever warms up with it,
  // e.g. a listener, stays warming. The warning logged above is the only indication of that.
  if (publish_status_.ok()) {
    init_target_.ready();
  }
}

} // namespace Router
} // namespace Envoy
