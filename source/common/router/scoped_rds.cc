#include "common/router/scoped_rds.h"

#include <memory>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/srds.pb.validate.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

// Types are deeply nested under Envoy::Config::ConfigProvider; use 'using-directives' across all
// ConfigProvider related types for consistency.
using Envoy::Config::ConfigProvider;
using Envoy::Config::ConfigProviderInstanceType;
using Envoy::Config::ConfigProviderManager;
using Envoy::Config::ConfigProviderPtr;

namespace Envoy {
namespace Router {

namespace ScopedRoutesConfigProviderUtil {

ConfigProviderPtr
create(const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
           config,
       Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
       ConfigProviderManager& scoped_routes_config_provider_manager) {
  ASSERT(config.route_specifier_case() == envoy::config::filter::network::http_connection_manager::
                                              v2::HttpConnectionManager::kScopedRoutes);

  switch (config.scoped_routes().config_specifier_case()) {
  case envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::
      kScopedRouteConfigurationsList: {
    const envoy::config::filter::network::http_connection_manager::v2::
        ScopedRouteConfigurationsList& scoped_route_list =
            config.scoped_routes().scoped_route_configurations_list();
    return scoped_routes_config_provider_manager.createStaticConfigProvider(
        RepeatedPtrUtil::convertToConstMessagePtrContainer<envoy::api::v2::ScopedRouteConfiguration,
                                                           ProtobufTypes::ConstMessagePtrVector>(
            scoped_route_list.scoped_route_configurations()),
        factory_context,
        ScopedRoutesConfigProviderManagerOptArg(config.scoped_routes().name(),
                                                config.scoped_routes().rds_config_source(),
                                                config.scoped_routes().scope_key_builder()));
  }
  case envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::kScopedRds:
    return scoped_routes_config_provider_manager.createXdsConfigProvider(
        config.scoped_routes().scoped_rds(), factory_context, stat_prefix,
        ScopedRoutesConfigProviderManagerOptArg(config.scoped_routes().name(),
                                                config.scoped_routes().rds_config_source(),
                                                config.scoped_routes().scope_key_builder()));
  default:
    // Proto validation enforces that is not reached.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace ScopedRoutesConfigProviderUtil

InlineScopedRoutesConfigProvider::InlineScopedRoutesConfigProvider(
    ProtobufTypes::ConstMessagePtrVector&& config_protos, std::string name,
    Server::Configuration::FactoryContext& factory_context,
    ScopedRoutesConfigProviderManager& config_provider_manager,
    envoy::api::v2::core::ConfigSource rds_config_source,
    envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder
        scope_key_builder)
    : Envoy::Config::ImmutableConfigProviderBase(factory_context, config_provider_manager,
                                                 ConfigProviderInstanceType::Inline,
                                                 ConfigProvider::ApiType::Delta),
      name_(std::move(name)),
      config_(std::make_shared<ScopedConfigImpl>(std::move(scope_key_builder))),
      config_protos_(std::make_move_iterator(config_protos.begin()),
                     std::make_move_iterator(config_protos.end())),
      rds_config_source_(std::move(rds_config_source)) {}

ScopedRdsConfigSubscription::ScopedRdsConfigSubscription(
    const envoy::config::filter::network::http_connection_manager::v2::ScopedRds& scoped_rds,
    const uint64_t manager_identifier, const std::string& name,
    const envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::
        ScopeKeyBuilder& scope_key_builder,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    ScopedRoutesConfigProviderManager& config_provider_manager)
    : DeltaConfigSubscriptionInstance("SRDS", manager_identifier, config_provider_manager,
                                      factory_context),
      name_(name), scope_key_builder_(scope_key_builder),
      scope_(factory_context.scope().createScope(stat_prefix + "scoped_rds." + name + ".")),
      stats_({ALL_SCOPED_RDS_STATS(POOL_COUNTER(*scope_))}),
      validation_visitor_(factory_context.messageValidationVisitor()) {
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          scoped_rds.scoped_rds_config_source(),
          Grpc::Common::typeUrl(
              envoy::api::v2::ScopedRouteConfiguration().GetDescriptor()->full_name()),
          *scope_, *this);

  initialize([scope_key_builder]() -> Envoy::Config::ConfigProvider::ConfigConstSharedPtr {
    return std::make_shared<ScopedConfigImpl>(
        envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder(
            scope_key_builder));
  });
}

void ScopedRdsConfigSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  std::vector<envoy::api::v2::ScopedRouteConfiguration> scoped_routes;
  for (const auto& resource_any : resources) {
    scoped_routes.emplace_back(MessageUtil::anyConvert<envoy::api::v2::ScopedRouteConfiguration>(
        resource_any, validation_visitor_));
  }

  std::unordered_set<std::string> resource_names;
  for (const auto& scoped_route : scoped_routes) {
    if (!resource_names.insert(scoped_route.name()).second) {
      throw EnvoyException(
          fmt::format("duplicate scoped route configuration {} found", scoped_route.name()));
    }
  }
  for (const auto& scoped_route : scoped_routes) {
    MessageUtil::validate(scoped_route);
  }

  // TODO(AndresGuedez): refactor such that it can be shared with other delta APIs (e.g., CDS).
  std::vector<std::string> exception_msgs;
  // We need to keep track of which scoped routes we might need to remove.
  ScopedConfigManager::ScopedRouteMap scoped_routes_to_remove =
      scoped_config_manager_.scopedRouteMap();
  for (auto& scoped_route : scoped_routes) {
    const std::string& scoped_route_name = scoped_route.name();
    scoped_routes_to_remove.erase(scoped_route_name);
    ScopedRouteInfoConstSharedPtr scoped_route_info =
        scoped_config_manager_.addOrUpdateRoutingScope(scoped_route, version_info);
    ENVOY_LOG(debug, "srds: add/update scoped_route '{}'", scoped_route_name);
    applyConfigUpdate([scoped_route_info](const ConfigProvider::ConfigConstSharedPtr& config)
                          -> ConfigProvider::ConfigConstSharedPtr {
      auto* thread_local_scoped_config =
          const_cast<ScopedConfigImpl*>(static_cast<const ScopedConfigImpl*>(config.get()));
      thread_local_scoped_config->addOrUpdateRoutingScope(scoped_route_info);
      return config;
    });
  }

  for (const auto& scoped_route : scoped_routes_to_remove) {
    const std::string scoped_route_name = scoped_route.first;
    ENVOY_LOG(debug, "srds: remove scoped route '{}'", scoped_route_name);
    scoped_config_manager_.removeRoutingScope(scoped_route_name);
    applyConfigUpdate([scoped_route_name](const ConfigProvider::ConfigConstSharedPtr& config)
                          -> ConfigProvider::ConfigConstSharedPtr {
      // In place update.
      auto* thread_local_scoped_config =
          const_cast<ScopedConfigImpl*>(static_cast<const ScopedConfigImpl*>(config.get()));
      thread_local_scoped_config->removeRoutingScope(scoped_route_name);
      return config;
    });
  }

  DeltaConfigSubscriptionInstance::onConfigUpdate();
  setLastConfigInfo(absl::optional<LastConfigInfo>({absl::nullopt, version_info}));
  stats_.config_reload_.inc();
}

ScopedRdsConfigProvider::ScopedRdsConfigProvider(
    ScopedRdsConfigSubscriptionSharedPtr&& subscription,
    envoy::api::v2::core::ConfigSource rds_config_source)
    : MutableConfigProviderCommonBase(std::move(subscription), ConfigProvider::ApiType::Delta),
      subscription_(static_cast<ScopedRdsConfigSubscription*>(
          MutableConfigProviderCommonBase::subscription_.get())),
      rds_config_source_(std::move(rds_config_source)) {}

ProtobufTypes::MessagePtr ScopedRoutesConfigProviderManager::dumpConfigs() const {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::ScopedRoutesConfigDump>();
  for (const auto& element : configSubscriptions()) {
    auto subscription = element.second.lock();
    ASSERT(subscription);

    if (subscription->configInfo()) {
      auto* dynamic_config = config_dump->mutable_dynamic_scoped_route_configs()->Add();
      dynamic_config->set_version_info(subscription->configInfo().value().last_config_version_);
      const ScopedRdsConfigSubscription* typed_subscription =
          static_cast<ScopedRdsConfigSubscription*>(subscription.get());
      dynamic_config->set_name(typed_subscription->name());
      const ScopedConfigManager::ScopedRouteMap& scoped_route_map =
          typed_subscription->scopedRouteMap();
      for (const auto& it : scoped_route_map) {
        dynamic_config->mutable_scoped_route_configs()->Add()->MergeFrom(it.second->config_proto_);
      }
      TimestampUtil::systemClockToTimestamp(subscription->lastUpdated(),
                                            *dynamic_config->mutable_last_updated());
    }
  }

  for (const auto& provider : immutableConfigProviders(ConfigProviderInstanceType::Inline)) {
    const auto protos_info =
        provider->configProtoInfoVector<envoy::api::v2::ScopedRouteConfiguration>();
    ASSERT(protos_info != absl::nullopt);
    auto* inline_config = config_dump->mutable_inline_scoped_route_configs()->Add();
    inline_config->set_name(static_cast<InlineScopedRoutesConfigProvider*>(provider)->name());
    for (const auto& config_proto : protos_info.value().config_protos_) {
      inline_config->mutable_scoped_route_configs()->Add()->MergeFrom(*config_proto);
    }
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *inline_config->mutable_last_updated());
  }

  return config_dump;
}

ConfigProviderPtr ScopedRoutesConfigProviderManager::createXdsConfigProvider(
    const Protobuf::Message& config_source_proto,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    const ConfigProviderManager::OptionalArg& optarg) {
  const auto& typed_optarg = static_cast<const ScopedRoutesConfigProviderManagerOptArg&>(optarg);
  ScopedRdsConfigSubscriptionSharedPtr subscription =
      ConfigProviderManagerImplBase::getSubscription<ScopedRdsConfigSubscription>(
          config_source_proto, factory_context.initManager(),
          [&config_source_proto, &factory_context, &stat_prefix,
           &typed_optarg](const uint64_t manager_identifier,
                          ConfigProviderManagerImplBase& config_provider_manager)
              -> Envoy::Config::ConfigSubscriptionCommonBaseSharedPtr {
            const auto& scoped_rds_config_source = dynamic_cast<
                const envoy::config::filter::network::http_connection_manager::v2::ScopedRds&>(
                config_source_proto);
            return std::make_shared<ScopedRdsConfigSubscription>(
                scoped_rds_config_source, manager_identifier, typed_optarg.scoped_routes_name_,
                typed_optarg.scope_key_builder_, factory_context, stat_prefix,
                static_cast<ScopedRoutesConfigProviderManager&>(config_provider_manager));
          });

  return std::make_unique<ScopedRdsConfigProvider>(std::move(subscription),
                                                   typed_optarg.rds_config_source_);
}

ConfigProviderPtr ScopedRoutesConfigProviderManager::createStaticConfigProvider(
    ProtobufTypes::ConstMessagePtrVector&& config_protos,
    Server::Configuration::FactoryContext& factory_context,
    const ConfigProviderManager::OptionalArg& optarg) {
  const auto& typed_optarg = static_cast<const ScopedRoutesConfigProviderManagerOptArg&>(optarg);
  return std::make_unique<InlineScopedRoutesConfigProvider>(
      std::move(config_protos), typed_optarg.scoped_routes_name_, factory_context, *this,
      typed_optarg.rds_config_source_, typed_optarg.scope_key_builder_);
}

} // namespace Router
} // namespace Envoy
