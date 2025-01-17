#include "source/common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"

namespace Envoy {
namespace Router {

// TODO(htuch): If support for multiple clusters is added per #1170 cluster_name_
RdsRouteConfigSubscription::RdsRouteConfigSubscription(
    RouteConfigUpdatePtr&& config_update,
    Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    const uint64_t manager_identifier, Server::Configuration::ServerFactoryContext& factory_context,
    const std::string& stat_prefix, Rds::RouteConfigProviderManager& route_config_provider_manager)
    : Rds::RdsRouteConfigSubscription(std::move(config_update), std::move(resource_decoder),
                                      rds.config_source(), rds.route_config_name(),
                                      manager_identifier, factory_context, stat_prefix + "rds.",
                                      "RDS", route_config_provider_manager),
      config_update_info_(static_cast<RouteConfigUpdateReceiver*>(
          Rds::RdsRouteConfigSubscription::config_update_info_.get())) {}

RdsRouteConfigSubscription::~RdsRouteConfigSubscription() { config_update_info_.release(); }

absl::Status RdsRouteConfigSubscription::beforeProviderUpdate(
    std::unique_ptr<Init::ManagerImpl>& noop_init_manager, std::unique_ptr<Cleanup>& resume_rds) {
  if (config_update_info_->protobufConfigurationCast().has_vhds() &&
      config_update_info_->vhdsConfigurationChanged()) {
    ENVOY_LOG(debug,
              "rds: vhds configuration present/changed, (re)starting vhds: config_name={} hash={}",
              route_config_name_, routeConfigUpdate()->configHash());
    ASSERT(config_update_info_->configInfo().has_value());
    maybeCreateInitManager(routeConfigUpdate()->configInfo().value().version_, noop_init_manager,
                           resume_rds);
    auto subscription_or_error = VhdsSubscription::createVhdsSubscription(
        config_update_info_, factory_context_, stat_prefix_, route_config_provider_);
    RETURN_IF_NOT_OK_REF(subscription_or_error.status());
    vhds_subscription_ = std::move(subscription_or_error.value());
    vhds_subscription_->registerInitTargetWithInitManager(
        noop_init_manager == nullptr ? local_init_manager_ : *noop_init_manager);
  }
  return absl::OkStatus();
}

void RdsRouteConfigSubscription::afterProviderUpdate() {
  // RDS update removed VHDS configuration
  if (!config_update_info_->protobufConfigurationCast().has_vhds()) {
    vhds_subscription_.release();
  }

  THROW_IF_NOT_OK(update_callback_manager_.runCallbacks());
}

// Initialize a no-op InitManager in case the one in the factory_context has completed
// initialization. This can happen if an RDS config update for an already established RDS
// subscription contains VHDS configuration.
void RdsRouteConfigSubscription::maybeCreateInitManager(
    const std::string& version_info, std::unique_ptr<Init::ManagerImpl>& init_manager,
    std::unique_ptr<Cleanup>& init_vhds) {
  if (local_init_manager_.state() == Init::Manager::State::Initialized) {
    init_manager = std::make_unique<Init::ManagerImpl>(
        fmt::format("VHDS {}:{}", route_config_name_, version_info));
    init_vhds = std::make_unique<Cleanup>([this, &init_manager, version_info] {
      // For new RDS subscriptions created after listener warming up, we don't wait for them to warm
      // up.
      Init::WatcherImpl noop_watcher(
          // Note: we just throw it away.
          fmt::format("VHDS ConfigUpdate watcher {}:{}", route_config_name_, version_info),
          []() { /*Do nothing.*/ });
      init_manager->initialize(noop_watcher);
    });
  }
}

void RdsRouteConfigSubscription::updateOnDemand(const std::string& aliases) {
  if (vhds_subscription_.get() == nullptr) {
    return;
  }
  vhds_subscription_->updateOnDemand(aliases);
}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    RdsRouteConfigSubscriptionSharedPtr&& subscription,
    Server::Configuration::ServerFactoryContext& factory_context)
    : base_(subscription, factory_context), config_update_info_(subscription->routeConfigUpdate()),
      factory_context_(factory_context) {
  // The subscription referenced by the 'base_' and by 'this' is the same.
  // In it the provider is already set by the 'base_' so it points to that.
  // Need to set again to point to 'this'.
  base_.subscription().routeConfigProvider() = this;
}

RdsRouteConfigSubscription& RdsRouteConfigProviderImpl::subscription() {
  return static_cast<RdsRouteConfigSubscription&>(base_.subscription());
}

absl::Status RdsRouteConfigProviderImpl::onConfigUpdate() {
  auto status = base_.onConfigUpdate();
  if (!status.ok()) {
    return status;
  }

  const auto aliases = config_update_info_->resourceIdsInLastVhdsUpdate();
  // Regular (non-VHDS) RDS updates don't populate aliases fields in resources.
  if (aliases.empty()) {
    return absl::OkStatus();
  }

  const auto config =
      std::static_pointer_cast<const ConfigImpl>(config_update_info_->parsedConfiguration());
  // Notifies connections that RouteConfiguration update has been propagated.
  // Callbacks processing is performed in FIFO order. The callback is skipped if alias used in
  // the VHDS update request do not match the aliases in the update response
  for (auto it = config_update_callbacks_.begin(); it != config_update_callbacks_.end();) {
    auto found = aliases.find(it->alias_);
    if (found != aliases.end()) {
      // TODO(dmitri-d) HeaderMapImpl is expensive, need to profile this
      auto host_header = Http::RequestHeaderMapImpl::create();
      host_header->setHost(VhdsSubscription::aliasToDomainName(it->alias_));
      const bool host_exists = config->virtualHostExists(*host_header);
      std::weak_ptr<Http::RouteConfigUpdatedCallback> current_cb(it->cb_);
      it->thread_local_dispatcher_.post([current_cb, host_exists] {
        if (auto cb = current_cb.lock()) {
          (*cb)(host_exists);
        }
      });
      it = config_update_callbacks_.erase(it);
    } else {
      it++;
    }
  }
  return absl::OkStatus();
}

ConfigConstSharedPtr RdsRouteConfigProviderImpl::configCast() const {
  ASSERT(dynamic_cast<const Config*>(RdsRouteConfigProviderImpl::config().get()));
  return std::static_pointer_cast<const Config>(RdsRouteConfigProviderImpl::config());
}

// Schedules a VHDS request on the main thread and queues up the callback to use when the VHDS
// response has been propagated to the worker thread that was the request origin.
void RdsRouteConfigProviderImpl::requestVirtualHostsUpdate(
    const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
    std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) {
  auto alias = VhdsSubscription::domainNameToAlias(
      config_update_info_->protobufConfigurationCast().name(), for_domain);
  // The RdsRouteConfigProviderImpl instance can go away before the dispatcher has a chance to
  // execute the callback. still_alive shared_ptr will be deallocated when the current instance of
  // the RdsRouteConfigProviderImpl is deallocated; we rely on a weak_ptr to still_alive flag to
  // determine if the RdsRouteConfigProviderImpl instance is still valid.
  factory_context_.mainThreadDispatcher().post([this,
                                                maybe_still_alive =
                                                    std::weak_ptr<bool>(still_alive_),
                                                alias, &thread_local_dispatcher,
                                                route_config_updated_cb]() -> void {
    if (maybe_still_alive.lock()) {
      subscription().updateOnDemand(alias);
      config_update_callbacks_.push_back({alias, thread_local_dispatcher, route_config_updated_cb});
    }
  });
}
RouteConfigProviderSharedPtr RdsFactoryImpl::createRdsRouteConfigProvider(
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager, ProtoTraitsImpl& proto_traits,
    Rds::RouteConfigProviderManager& manager) {
  auto provider = manager.addDynamicProvider(
      rds, rds.route_config_name(), init_manager,
      [&factory_context, &rds, &stat_prefix, &manager, &proto_traits](uint64_t manager_identifier) {
        auto config_update =
            std::make_unique<RouteConfigUpdateReceiverImpl>(proto_traits, factory_context);
        auto resource_decoder = std::make_shared<
            Envoy::Config::OpaqueResourceDecoderImpl<envoy::config::route::v3::RouteConfiguration>>(
            factory_context.messageValidationContext().dynamicValidationVisitor(), "name");
        auto subscription = std::make_shared<RdsRouteConfigSubscription>(
            std::move(config_update), std::move(resource_decoder), rds, manager_identifier,
            factory_context, stat_prefix, manager);
        auto provider =
            std::make_shared<RdsRouteConfigProviderImpl>(std::move(subscription), factory_context);
        return std::make_pair(provider, &provider->subscription().initTarget());
      });
  ASSERT(dynamic_cast<RouteConfigProvider*>(provider.get()));
  return std::static_pointer_cast<RouteConfigProvider>(provider);
}

REGISTER_FACTORY(RdsFactoryImpl, RdsFactory);

} // namespace Router
} // namespace Envoy
