#include "common/router/scoped_rds.h"

#include <memory>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/srds.pb.validate.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/config/resources.h"
#include "common/init/manager_impl.h"
#include "common/init/watcher_impl.h"

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
    envoy::api::v2::core::ConfigSource rds_config_source,
    RouteConfigProviderManager& route_config_provider_manager,
    ScopedRoutesConfigProviderManager& config_provider_manager)
    : DeltaConfigSubscriptionInstance("SRDS", manager_identifier, config_provider_manager,
                                      factory_context),
      factory_context_(factory_context), name_(name), scope_key_builder_(scope_key_builder),
      scope_(factory_context.scope().createScope(stat_prefix + "scoped_rds." + name + ".")),
      stats_({ALL_SCOPED_RDS_STATS(POOL_COUNTER(*scope_))}),
      rds_config_source_(std::move(rds_config_source)),
      validation_visitor_(factory_context.messageValidationVisitor()), stat_prefix_(stat_prefix),
      route_config_provider_manager_(route_config_provider_manager) {
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
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  bool any_applied = false;
  std::vector<std::string> exception_msgs;
  absl::flat_hash_set<std::string> unique_resource_names;
  envoy::config::filter::network::http_connection_manager::v2::Rds rds;
  rds.mutable_config_source()->MergeFrom(rds_config_source_);

  std::unique_ptr<Init::ManagerImpl> overriding_init_manager;
  if (factory_context_.initManager().state() == Init::Manager::State::Initialized) {
    // Pause RDS to not send a burst of RDS requests until we start all the new subscriptions.
    factory_context_.clusterManager().adsMux().pause(
        Envoy::Config::TypeUrl::get().RouteConfiguration);
    overriding_init_manager =
        std::make_unique<Init::ManagerImpl>(fmt::format("SRDS {}:{}", name_, version_info));
  }

  for (const auto& resource : added_resources) {
    envoy::api::v2::ScopedRouteConfiguration scoped_route_config;
    try {
      scoped_route_config = MessageUtil::anyConvert<envoy::api::v2::ScopedRouteConfiguration>(
          resource.resource(), validation_visitor_);
      MessageUtil::validate(scoped_route_config);
      if (!unique_resource_names.insert(scoped_route_config.name()).second) {
        throw EnvoyException(fmt::format("duplicate scoped route configuration '{}' found",
                                         scoped_route_config.name()));
      }
      rds.set_route_config_name(scoped_route_config.route_configuration_name());
      ScopedRouteInfoConstSharedPtr scoped_route_info = std::make_shared<ScopedRouteInfo>(
          std::move(scoped_route_config),
          route_config_provider_manager_.createRdsRouteConfigProvider(
              rds, factory_context_, stat_prefix_,
              (overriding_init_manager == nullptr ? factory_context_.initManager()
                                                  : *overriding_init_manager)));
      // Detect if there is key conflict between two scopes.
      auto iter = scope_name_by_hash_.find(scoped_route_info->scopeKey().hash());
      if (iter != scope_name_by_hash_.end() && iter->second != scoped_route_info->scopeName()) {
        throw EnvoyException(
            fmt::format("scope key conflict found, first scope is '{}', second scope is '{}'",
                        iter->second, scoped_route_info->scopeName()));
      }
      scope_name_by_hash_[scoped_route_info->scopeKey().hash()] = scoped_route_info->scopeName();
      scoped_route_map_[scoped_route_info->scopeName()] = scoped_route_info;
      applyConfigUpdate([scoped_route_info](ConfigProvider::ConfigConstSharedPtr config)
                            -> ConfigProvider::ConfigConstSharedPtr {
        auto* thread_local_scoped_config =
            const_cast<ScopedConfigImpl*>(static_cast<const ScopedConfigImpl*>(config.get()));

        thread_local_scoped_config->addOrUpdateRoutingScope(scoped_route_info);
        return config;
      });
      any_applied = true;
      ENVOY_LOG(debug, "srds: add/update scoped_route '{}'", scoped_route_info->scopeName());
    } catch (const EnvoyException& e) {
      exception_msgs.emplace_back(fmt::format("{}", e.what()));
    }
  }
  if (overriding_init_manager != nullptr) {
    // For new RDS subscriptions created after listener warming up, we don't wait for them to warm
    // up.
    Init::WatcherImpl noop_watcher(
        // Note: we just throw it away.
        fmt::format("SRDS ConfigUpdate watcher {}:{}", name_, version_info),
        []() { ENVOY_LOG_MISC(trace, ""); });
    overriding_init_manager->initialize(noop_watcher);
    // New subscriptions should be created, now lift the floodgate.
    factory_context_.clusterManager().adsMux().resume(
        Envoy::Config::TypeUrl::get().RouteConfiguration);
  }

  for (const auto& scope_name : removed_resources) {
    auto iter = scoped_route_map_.find(scope_name);
    if (iter != scoped_route_map_.end()) {
      ScopedRouteInfoConstSharedPtr to_be_deleted = iter->second;
      scope_name_by_hash_.erase(iter->second->scopeKey().hash());
      scoped_route_map_.erase(iter);
      applyConfigUpdate(
          [scope_name](
              ConfigProvider::ConfigConstSharedPtr config) -> ConfigProvider::ConfigConstSharedPtr {
            auto* thread_local_scoped_config =
                const_cast<ScopedConfigImpl*>(static_cast<const ScopedConfigImpl*>(config.get()));
            thread_local_scoped_config->removeRoutingScope(scope_name);
            return config;
          },
          // We need to delete the associated RouteConfigProvider in main thread.
          [to_be_deleted]() { /*to_be_deleted is destructed in main thread.*/ });
      any_applied = true;
      ENVOY_LOG(debug, "srds: remove scoped route '{}'", scope_name);
    }
  }
  ConfigSubscriptionCommonBase::onConfigUpdate();
  if (any_applied) {
    setLastConfigInfo(absl::optional<LastConfigInfo>({absl::nullopt, version_info}));
  }
  stats_.config_reload_.inc();
  if (!exception_msgs.empty()) {
    throw EnvoyException(fmt::format("Error adding/updating scoped route(s): {}",
                                     StringUtil::join(exception_msgs, ", ")));
  }
}

// TODO(stevenzzzz): see issue #7508, consider generalizing this function as it overlaps with
// CdsApiImpl::onConfigUpdate.
void ScopedRdsConfigSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  absl::flat_hash_map<std::string, envoy::api::v2::ScopedRouteConfiguration> scoped_routes;
  absl::flat_hash_map<uint64_t, std::string> scope_name_by_key_hash;
  for (const auto& resource_any : resources) {
    // Throws (thus rejects all) on any error.
    auto scoped_route = MessageUtil::anyConvert<envoy::api::v2::ScopedRouteConfiguration>(
        resource_any, validation_visitor_);
    MessageUtil::validate(scoped_route);
    const std::string scope_name = scoped_route.name();
    auto scope_config_inserted = scoped_routes.try_emplace(scope_name, std::move(scoped_route));
    if (!scope_config_inserted.second) {
      throw EnvoyException(
          fmt::format("duplicate scoped route configuration '{}' found", scope_name));
    }
    const envoy::api::v2::ScopedRouteConfiguration& scoped_route_config =
        scope_config_inserted.first->second;
    uint64_t key_fingerprint = MessageUtil::hash(scoped_route_config.key());
    if (!scope_name_by_key_hash.try_emplace(key_fingerprint, scope_name).second) {
      throw EnvoyException(
          fmt::format("scope key conflict found, first scope is '{}', second scope is '{}'",
                      scope_name_by_key_hash[key_fingerprint], scope_name));
    }
  }
  ScopedRouteMap scoped_routes_to_remove = scoped_route_map_;
  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> to_add_repeated;
  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  for (auto& iter : scoped_routes) {
    const std::string& scope_name = iter.first;
    scoped_routes_to_remove.erase(scope_name);
    auto* to_add = to_add_repeated.Add();
    to_add->set_name(scope_name);
    to_add->set_version(version_info);
    to_add->mutable_resource()->PackFrom(iter.second);
  }

  for (const auto& scoped_route : scoped_routes_to_remove) {
    *to_remove_repeated.Add() = scoped_route.first;
  }
  onConfigUpdate(to_add_repeated, to_remove_repeated, version_info);
} // namespace Router

ScopedRdsConfigProvider::ScopedRdsConfigProvider(
    ScopedRdsConfigSubscriptionSharedPtr&& subscription,
    envoy::api::v2::core::ConfigSource rds_config_source)
    : MutableConfigProviderCommonBase(std::move(subscription), ConfigProvider::ApiType::Delta),
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
      const ScopedRouteMap& scoped_route_map = typed_subscription->scopedRouteMap();
      for (const auto& it : scoped_route_map) {
        dynamic_config->mutable_scoped_route_configs()->Add()->MergeFrom(it.second->configProto());
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
                typed_optarg.rds_config_source_,
                static_cast<ScopedRoutesConfigProviderManager&>(config_provider_manager)
                    .route_config_provider_manager(),
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
