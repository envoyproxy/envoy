#include "common/router/scoped_rds.h"

#include <memory>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/srds.pb.validate.h"

#include "common/common/assert.h"
#include "common/config/subscription_factory.h"
#include "common/router/scoped_config_impl.h"

namespace Envoy {
namespace Router {

Envoy::Config::ConfigProviderPtr ScopedRoutesConfigProviderUtil::maybeCreate(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        config,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    Envoy::Config::ConfigProviderManager& scoped_routes_config_provider_manager) {
  switch (config.scoped_routes_specifier_case()) {
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      kScopedRoutesConfig:
    return scoped_routes_config_provider_manager.createStaticConfigProvider(
        config.scoped_routes_config(), factory_context,
        ScopedRoutesConfigProviderManagerOptArg(config.rds()));

  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      kScopedRds:
    return scoped_routes_config_provider_manager.createXdsConfigProvider(
        config.scoped_rds(), factory_context, stat_prefix,
        ScopedRoutesConfigProviderManagerOptArg(config.rds()));

  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      SCOPED_ROUTES_SPECIFIER_NOT_SET:
    return nullptr;

  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

InlineScopedRoutesConfigProvider::InlineScopedRoutesConfigProvider(
    const envoy::api::v2::ScopedRouteConfigurationsSet& config_proto,
    Server::Configuration::FactoryContext& factory_context,
    ScopedRoutesConfigProviderManager& config_provider_manager,
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds)
    : Envoy::Config::ImmutableConfigProviderImplBase(
          factory_context, config_provider_manager,
          Envoy::Config::ConfigProviderInstanceType::Inline),
      config_(std::make_shared<NullScopedConfigImpl>()), config_proto_(config_proto), rds_(rds) {}

ScopedRdsConfigSubscription::ScopedRdsConfigSubscription(
    const envoy::config::filter::network::http_connection_manager::v2::ScopedRds& scoped_rds,
    const std::string& manager_identifier, Server::Configuration::FactoryContext& factory_context,
    const std::string& stat_prefix, ScopedRoutesConfigProviderManager& config_provider_manager)
    : ConfigSubscriptionInstanceBase(
          "SRDS", manager_identifier, config_provider_manager, factory_context.timeSource(),
          factory_context.timeSource().systemTime(), factory_context.localInfo()),
      scoped_routes_config_name_(scoped_rds.scoped_routes_config_set_name()),
      scope_(factory_context.scope().createScope(stat_prefix + "scoped_rds." +
                                                 scoped_routes_config_name_ + ".")),
      stats_({ALL_SCOPED_RDS_STATS(POOL_COUNTER(*scope_))}) {
  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::ScopedRouteConfigurationsSet>(
      scoped_rds.config_source(), factory_context.localInfo(), factory_context.dispatcher(),
      factory_context.clusterManager(), factory_context.random(), *scope_,
      "envoy.api.v2.ScopedRoutesDiscoveryService.FetchScopedRoutes",
      "envoy.api.v2.ScopedRoutesDiscoveryService.StreamScopedRoutes", factory_context.api());
}

void ScopedRdsConfigSubscription::onConfigUpdate(const ResourceVector& resources,
                                                 const std::string& version_info) {
  if (resources.empty()) {
    ENVOY_LOG(debug, "Missing ScopedRouteConfigurationsSet for {} in onConfigUpdate()",
              scoped_routes_config_name_);
    stats_.update_empty_.inc();
    ConfigSubscriptionInstanceBase::onConfigUpdateFailed();
    return;
  }

  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected SRDS resource length: {}", resources.size()));
  }

  const auto& scoped_routes_config = resources[0];
  MessageUtil::validate(scoped_routes_config);
  // The name we receive in the config must match the name requested.
  if (scoped_routes_config.name() != scoped_routes_config_name_) {
    throw EnvoyException(fmt::format("Unexpected SRDS configuration (expecting {}): {}",
                                     scoped_routes_config_name_, scoped_routes_config.name()));
  }

  if (checkAndApplyConfig(scoped_routes_config, scoped_routes_config_name_, version_info)) {
    scoped_routes_proto_ = scoped_routes_config;
    stats_.config_reload_.inc();
  }

  ConfigSubscriptionInstanceBase::onConfigUpdate();
}

ScopedRdsConfigProvider::ScopedRdsConfigProvider(
    ScopedRdsConfigSubscriptionSharedPtr&& subscription,
    Envoy::Config::ConfigProvider::ConfigConstSharedPtr initial_config,
    Server::Configuration::FactoryContext& factory_context,
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds)
    : MutableConfigProviderImplBase(std::move(subscription), factory_context),
      subscription_(static_cast<ScopedRdsConfigSubscription*>(
          MutableConfigProviderImplBase::subscription().get())),
      rds_(rds) {
  initialize(initial_config);
}

Envoy::Config::ConfigProvider::ConfigConstSharedPtr
ScopedRdsConfigProvider::onConfigProtoUpdate(const Protobuf::Message& config_proto) {
  return std::make_shared<ScopedConfigImpl>(
      static_cast<const envoy::api::v2::ScopedRouteConfigurationsSet&>(config_proto));
}

ProtobufTypes::MessagePtr ScopedRoutesConfigProviderManager::dumpConfigs() const {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::ScopedRoutesConfigDump>();
  for (const auto& element : configSubscriptions()) {
    auto subscription = element.second.lock();
    ASSERT(subscription);

    if (subscription->configInfo()) {
      auto* dynamic_config = config_dump->mutable_dynamic_scoped_routes_configs()->Add();
      dynamic_config->set_version_info(subscription->configInfo().value().last_config_version_);
      dynamic_config->mutable_scoped_routes_config()->MergeFrom(
          static_cast<ScopedRdsConfigSubscription*>(subscription.get())->configProto().value());
      TimestampUtil::systemClockToTimestamp(subscription->lastUpdated(),
                                            *dynamic_config->mutable_last_updated());
    }
  }

  for (const auto& provider :
       immutableConfigProviders(Envoy::Config::ConfigProviderInstanceType::Inline)) {
    ASSERT(provider->configProtoInfo<envoy::api::v2::ScopedRouteConfigurationsSet>());
    auto* inline_config = config_dump->mutable_inline_scoped_routes_configs()->Add();
    inline_config->mutable_scoped_routes_config()->MergeFrom(
        provider->configProtoInfo<envoy::api::v2::ScopedRouteConfigurationsSet>()
            .value()
            .config_proto_);
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *inline_config->mutable_last_updated());
  }

  return config_dump;
}

Envoy::Config::ConfigProviderPtr ScopedRoutesConfigProviderManager::createXdsConfigProvider(
    const Protobuf::Message& config_source_proto,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) {
  ScopedRdsConfigSubscriptionSharedPtr subscription =
      ConfigProviderManagerImplBase::getSubscription<ScopedRdsConfigSubscription>(
          config_source_proto, factory_context.initManager(),
          [&config_source_proto, &factory_context,
           &stat_prefix](const std::string& manager_identifier,
                         ConfigProviderManagerImplBase& config_provider_manager)
              -> Envoy::Config::ConfigSubscriptionInstanceBaseSharedPtr {
            const auto& scoped_rds_config_source = dynamic_cast<
                const envoy::config::filter::network::http_connection_manager::v2::ScopedRds&>(
                config_source_proto);
            return std::make_shared<ScopedRdsConfigSubscription>(
                scoped_rds_config_source, manager_identifier, factory_context, stat_prefix,
                static_cast<ScopedRoutesConfigProviderManager&>(config_provider_manager));
          });

  Envoy::Config::ConfigProvider::ConfigConstSharedPtr initial_config;
  const Envoy::Config::MutableConfigProviderImplBase* first_provider =
      subscription->getAnyBoundMutableConfigProvider();
  if (first_provider != nullptr) {
    initial_config = first_provider->getConfig();
  }
  const auto& typed_optarg = static_cast<const ScopedRoutesConfigProviderManagerOptArg&>(optarg);
  return std::make_unique<ScopedRdsConfigProvider>(std::move(subscription), initial_config,
                                                   factory_context, typed_optarg.rds_);
}

Envoy::Config::ConfigProviderPtr ScopedRoutesConfigProviderManager::createStaticConfigProvider(
    const Protobuf::Message& config_proto, Server::Configuration::FactoryContext& factory_context,
    const Envoy::Config::ConfigProviderManager::OptionalArg& optarg) {
  const auto& scoped_routes_proto =
      dynamic_cast<const envoy::api::v2::ScopedRouteConfigurationsSet&>(config_proto);
  const auto& typed_optarg = static_cast<const ScopedRoutesConfigProviderManagerOptArg&>(optarg);
  return absl::make_unique<InlineScopedRoutesConfigProvider>(scoped_routes_proto, factory_context,
                                                             *this, typed_optarg.rds_);
}

} // namespace Router
} // namespace Envoy
