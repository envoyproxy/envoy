#pragma once

#include <memory>

#include "envoy/singleton/instance.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/resource_name.h"
#include "source/common/rds/common/config_traits_impl.h"
#include "source/common/rds/common/proto_traits_impl.h"
#include "source/common/rds/common/route_config_provider_manager.h"
#include "source/common/rds/rds_route_config_provider_impl.h"
#include "source/common/rds/rds_route_config_subscription.h"
#include "source/common/rds/route_config_provider_manager.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/rds/static_route_config_provider_impl.h"

#include "absl/strings/ascii.h"

namespace Envoy {
namespace Rds {
namespace Common {

/**
 * Implementation of RouteConfigProviderManager interface.
 * The keys for config tracker, stat, log and exception text will be derived
 * from the name of the proto message class passed in the Rds template argument.
 * Since the config tracker key has to be unique across envoy, this name has to be also unique.
 * The following two fields must be declared in the proto message since the template will call
 * their generated member functions:
 * config_source (config.core.v3.ConfigSource)
 * route_config_name (string)
 */
template <class Rds, class RouteConfiguration, int NameFieldNumber, class ConfigImpl,
          class NullConfigImpl>
class RouteConfigProviderManagerImpl : public RouteConfigProviderManager<Rds, RouteConfiguration>,
                                       public Singleton::Instance {
public:
  RouteConfigProviderManagerImpl(OptRef<Server::Admin> admin)
      : manager_(admin, absl::AsciiStrToLower(getRdsName()) + "_routes", proto_traits_) {}

  // RouteConfigProviderManager
  RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const Rds& rds, Server::Configuration::ServerFactoryContext& factory_context,
      const std::string& stat_prefix, Init::Manager& init_manager) override {
    return manager_.addDynamicProvider(
        rds, rds.route_config_name(), init_manager,
        [&factory_context, &rds, &stat_prefix, this](uint64_t manager_identifier) {
          auto config_update = std::make_unique<RouteConfigUpdateReceiverImpl>(
              config_traits_, proto_traits_, factory_context);
          auto resource_decoder =
              std::make_shared<Envoy::Config::OpaqueResourceDecoderImpl<RouteConfiguration>>(
                  factory_context.messageValidationContext().dynamicValidationVisitor(),
                  getNameFieldName());
          auto subscription = std::make_shared<RdsRouteConfigSubscription>(
              std::move(config_update), std::move(resource_decoder), rds.config_source(),
              rds.route_config_name(), manager_identifier, factory_context,
              stat_prefix + absl::AsciiStrToLower(getRdsName()) + ".",
              absl::AsciiStrToUpper(getRdsName()), manager_);
          auto provider = std::make_shared<RdsRouteConfigProviderImpl>(std::move(subscription),
                                                                       factory_context);
          return std::make_pair(provider, &provider->subscription().initTarget());
        });
  }

  RouteConfigProviderPtr createStaticRouteConfigProvider(
      const RouteConfiguration& route_config,
      Server::Configuration::ServerFactoryContext& factory_context) override {
    return manager_.addStaticProvider([&factory_context, &route_config, this]() {
      return std::make_unique<StaticRouteConfigProviderImpl>(route_config, config_traits_,
                                                             factory_context, manager_);
    });
  }

private:
  Envoy::Rds::RouteConfigProviderManager manager_;
  ConfigTraitsImpl<RouteConfiguration, ConfigImpl, NullConfigImpl> config_traits_;
  ProtoTraitsImpl<RouteConfiguration, 1> proto_traits_;

  std::string getRdsName() { return Rds().GetDescriptor()->name(); }

  std::string getNameFieldName() {
    ASSERT(RouteConfiguration().GetDescriptor()->FindFieldByNumber(NameFieldNumber));
    return RouteConfiguration().GetDescriptor()->FindFieldByNumber(NameFieldNumber)->name();
  }
};

} // namespace Common
} // namespace Rds
} // namespace Envoy
