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
                                      manager_identifier, factory_context, stat_prefix, "RDS",
                                      route_config_provider_manager, creation_status),
      config_update_info_(static_cast<RouteConfigUpdateReceiver*>(
          Rds::RdsRouteConfigSubscription::config_update_info_.get())) {}

RdsRouteConfigSubscription::~RdsRouteConfigSubscription() { config_update_info_.release(); }

absl::Status RdsRouteConfigSubscription::afterProviderUpdate() {
  return update_callback_manager_.runCallbacks();
}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    RdsRouteConfigSubscriptionSharedPtr&& subscription,
    Server::Configuration::ServerFactoryContext& factory_context)
    : base_(subscription, factory_context), config_update_info_(subscription->routeConfigUpdate()) {
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
  config_update_info_->onVhdsConfigPublished();
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
  config_update_info_->requestVirtualHostsUpdate(for_domain, thread_local_dispatcher,
                                                 route_config_updated_cb);
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
            proto_traits, factory_context, stat_prefix, /*from_rds=*/true);
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
