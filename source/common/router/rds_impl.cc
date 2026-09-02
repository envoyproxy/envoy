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
#include "source/common/router/vhds.h"

namespace Envoy {
namespace Router {

absl::StatusOr<std::unique_ptr<RdsRouteConfigSubscription>> RdsRouteConfigSubscription::create(
    RouteConfigUpdatePtr&& config_update,
    Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    const uint64_t manager_identifier, Server::Configuration::ServerFactoryContext& factory_context,
    const std::string& stat_prefix,
    Rds::RouteConfigProviderManager& route_config_provider_manager) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<RdsRouteConfigSubscription>(new RdsRouteConfigSubscription(
      std::move(config_update), std::move(resource_decoder), rds, manager_identifier,
      factory_context, stat_prefix, route_config_provider_manager, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

// TODO(htuch): If support for multiple clusters is added per #1170 cluster_name_
RdsRouteConfigSubscription::RdsRouteConfigSubscription(
    RouteConfigUpdatePtr&& config_update,
    Envoy::Config::OpaqueResourceDecoderSharedPtr&& resource_decoder,
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    const uint64_t manager_identifier, Server::Configuration::ServerFactoryContext& factory_context,
    const std::string& stat_prefix, Rds::RouteConfigProviderManager& route_config_provider_manager,
    absl::Status& creation_status)
    : Rds::RdsRouteConfigSubscription(std::move(config_update), std::move(resource_decoder),
                                      rds.config_source(), rds.route_config_name(),
                                      manager_identifier, factory_context, stat_prefix + "rds.",
                                      "RDS", route_config_provider_manager, creation_status),
      config_update_info_(static_cast<RouteConfigUpdateReceiver*>(
          Rds::RdsRouteConfigSubscription::config_update_info_.get())) {}

RdsRouteConfigSubscription::~RdsRouteConfigSubscription() { config_update_info_.release(); }

absl::Status RdsRouteConfigSubscription::afterProviderUpdate() {
  return update_callback_manager_.runCallbacks();
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
  factory_context_.mainThreadDispatcher().post(
      [this, maybe_still_alive = std::weak_ptr<bool>(still_alive_), alias, &thread_local_dispatcher,
       route_config_updated_cb]() -> void {
        if (maybe_still_alive.lock()) {
          subscription().updateOnDemand(alias);
          config_update_callbacks_.push_back(
              {alias, thread_local_dispatcher, route_config_updated_cb});
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
        auto config_update = std::make_unique<RouteConfigUpdateReceiverImpl>(
            proto_traits, factory_context, stat_prefix + "rds.");
        auto resource_decoder = std::make_shared<
            Envoy::Config::OpaqueResourceDecoderImpl<envoy::config::route::v3::RouteConfiguration>>(
            factory_context.messageValidationContext().dynamicValidationVisitor(), "name");
        auto subscription =
            THROW_OR_RETURN_VALUE(RdsRouteConfigSubscription::create(
                                      std::move(config_update), std::move(resource_decoder), rds,
                                      manager_identifier, factory_context, stat_prefix, manager),
                                  std::unique_ptr<RdsRouteConfigSubscription>);
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
