#include "source/common/router/scoped_rds.h"

#include <memory>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/config/resource_name.h"
#include "source/common/config/xds_resource.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/watcher_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/scoped_config_impl.h"

#include "absl/strings/str_join.h"

// Types are deeply nested under Envoy::Config::ConfigProvider; use 'using-directives' across all
// ConfigProvider related types for consistency.
using Envoy::Config::ConfigProvider;
using Envoy::Config::ConfigProviderInstanceType;
using Envoy::Config::ConfigProviderManager;
using Envoy::Config::ConfigProviderPtr;
using Envoy::Config::ScopedResume;

namespace Envoy {
namespace Router {
namespace ScopedRoutesConfigProviderUtil {
ConfigProviderPtr create(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config,
    Server::Configuration::ServerFactoryContext& factory_context, Init::Manager& init_manager,
    const std::string& stat_prefix, ConfigProviderManager& scoped_routes_config_provider_manager) {
  ASSERT(config.route_specifier_case() ==
         envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
             RouteSpecifierCase::kScopedRoutes);
  switch (config.scoped_routes().config_specifier_case()) {
  case envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes::
      ConfigSpecifierCase::kScopedRouteConfigurationsList: {
    const envoy::extensions::filters::network::http_connection_manager::v3::
        ScopedRouteConfigurationsList& scoped_route_list =
            config.scoped_routes().scoped_route_configurations_list();
    return scoped_routes_config_provider_manager.createStaticConfigProvider(
        RepeatedPtrUtil::convertToConstMessagePtrContainer<
            envoy::config::route::v3::ScopedRouteConfiguration,
            ProtobufTypes::ConstMessagePtrVector>(scoped_route_list.scoped_route_configurations()),
        factory_context,
        ScopedRoutesConfigProviderManagerOptArg(config.scoped_routes().name(),
                                                config.scoped_routes().rds_config_source()));
  }
  case envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes::
      ConfigSpecifierCase::kScopedRds:
    return scoped_routes_config_provider_manager.createXdsConfigProvider(
        config.scoped_routes().scoped_rds(), factory_context, init_manager, stat_prefix,
        ScopedRoutesConfigProviderManagerOptArg(config.scoped_routes().name(),
                                                config.scoped_routes().rds_config_source()));
  case envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes::
      ConfigSpecifierCase::CONFIG_SPECIFIER_NOT_SET:
    PANIC("not implemented");
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

ScopeKeyBuilderPtr createScopeKeyBuilder(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config) {
  ASSERT(config.route_specifier_case() ==
         envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
             RouteSpecifierCase::kScopedRoutes);
  auto scope_key_builder = config.scoped_routes().scope_key_builder();
  return std::make_unique<ScopeKeyBuilderImpl>(std::move(scope_key_builder));
}

} // namespace ScopedRoutesConfigProviderUtil

namespace {

std::vector<ScopedRouteInfoConstSharedPtr>
makeScopedRouteInfos(ProtobufTypes::ConstMessagePtrVector&& config_protos,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     ScopedRoutesConfigProviderManager& config_provider_manager) {
  std::vector<ScopedRouteInfoConstSharedPtr> scopes;
  for (std::unique_ptr<const Protobuf::Message>& config_proto : config_protos) {
    auto scoped_route_config =
        MessageUtil::downcastAndValidate<const envoy::config::route::v3::ScopedRouteConfiguration&>(
            *config_proto, factory_context.messageValidationContext().staticValidationVisitor());
    if (!scoped_route_config.route_configuration_name().empty()) {
      throw EnvoyException("Fetching routes via RDS (route_configuration_name) is not supported "
                           "with inline scoped routes.");
    }
    if (!scoped_route_config.has_route_configuration()) {
      throw EnvoyException("You must specify a route_configuration with inline scoped routes.");
    }
    RouteConfigProviderPtr route_config_provider =
        config_provider_manager.routeConfigProviderManager().createStaticRouteConfigProvider(
            scoped_route_config.route_configuration(), factory_context,
            factory_context.messageValidationContext().staticValidationVisitor());
    scopes.push_back(std::make_shared<const ScopedRouteInfo>(std::move(scoped_route_config),
                                                             route_config_provider->configCast()));
  }

  return scopes;
}

} // namespace

InlineScopedRoutesConfigProvider::InlineScopedRoutesConfigProvider(
    ProtobufTypes::ConstMessagePtrVector&& config_protos, std::string name,
    Server::Configuration::ServerFactoryContext& factory_context,
    ScopedRoutesConfigProviderManager& config_provider_manager,
    envoy::config::core::v3::ConfigSource rds_config_source)
    : Envoy::Config::ImmutableConfigProviderBase(factory_context, config_provider_manager,
                                                 ConfigProviderInstanceType::Inline,
                                                 ConfigProvider::ApiType::Delta),
      name_(std::move(name)),
      scopes_(
          makeScopedRouteInfos(std::move(config_protos), factory_context, config_provider_manager)),
      config_(std::make_shared<ScopedConfigImpl>(scopes_)),
      rds_config_source_(std::move(rds_config_source)) {}

ScopedRdsConfigSubscription::ScopedRdsConfigSubscription(
    const envoy::extensions::filters::network::http_connection_manager::v3::ScopedRds& scoped_rds,
    const uint64_t manager_identifier, const std::string& name,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    envoy::config::core::v3::ConfigSource rds_config_source,
    RouteConfigProviderManager& route_config_provider_manager,
    ScopedRoutesConfigProviderManager& config_provider_manager)
    : DeltaConfigSubscriptionInstance("SRDS", manager_identifier, config_provider_manager,
                                      factory_context),
      Envoy::Config::SubscriptionBase<envoy::config::route::v3::ScopedRouteConfiguration>(
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      factory_context_(factory_context), name_(name),
      scope_(factory_context.scope().createScope(stat_prefix + "scoped_rds." + name + ".")),
      stats_({ALL_SCOPED_RDS_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))}),
      rds_config_source_(std::move(rds_config_source)), stat_prefix_(stat_prefix),
      route_config_provider_manager_(route_config_provider_manager) {
  const auto resource_name = getResourceName();
  if (scoped_rds.srds_resources_locator().empty()) {
    subscription_ = THROW_OR_RETURN_VALUE(
        factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
            scoped_rds.scoped_rds_config_source(), Grpc::Common::typeUrl(resource_name), *scope_,
            *this, resource_decoder_, {}),
        Envoy::Config::SubscriptionPtr);
  } else {
    const auto srds_resources_locator = THROW_OR_RETURN_VALUE(
        Envoy::Config::XdsResourceIdentifier::decodeUrl(scoped_rds.srds_resources_locator()),
        xds::core::v3::ResourceLocator);
    subscription_ = THROW_OR_RETURN_VALUE(
        factory_context.clusterManager().subscriptionFactory().collectionSubscriptionFromUrl(
            srds_resources_locator, scoped_rds.scoped_rds_config_source(), resource_name, *scope_,
            *this, resource_decoder_),
        Envoy::Config::SubscriptionPtr);
  }

  // TODO(tony612): consider not using the callback here.
  initialize([]() -> Envoy::Config::ConfigProvider::ConfigConstSharedPtr {
    return std::make_shared<ScopedConfigImpl>();
  });
}

// Constructor for RdsRouteConfigProviderHelper when scope is eager loading.
// Initialize RdsRouteConfigProvider by default.
ScopedRdsConfigSubscription::RdsRouteConfigProviderHelper::RdsRouteConfigProviderHelper(
    ScopedRdsConfigSubscription& parent, std::string scope_name,
    envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    Init::Manager& init_manager)
    : parent_(parent), scope_name_(scope_name), on_demand_(false) {
  initRdsConfigProvider(rds, init_manager);
}

// Constructor for RdsRouteConfigProviderHelper when scope is on demand.
// Leave the RdsRouteConfigProvider uninitialized.
ScopedRdsConfigSubscription::RdsRouteConfigProviderHelper::RdsRouteConfigProviderHelper(
    ScopedRdsConfigSubscription& parent, std::string scope_name)
    : parent_(parent), scope_name_(scope_name), on_demand_(true) {
  parent_.stats_.on_demand_scopes_.inc();
}

// When on demand callback is received from main thread, there are 4 cases.
// 1. Scope is not found, post a scope not found callback back to worker thread.
// 2. Scope is found but route provider has not been initialized, create route provider.
// 3. After route provider has been initialized, if RouteConfiguration has been fetched,
// post scope found callback to worker thread.
// 4. After route provider has been initialized, if RouteConfiguration is null,
// cache the callback and wait for RouteConfiguration to come.
void ScopedRdsConfigSubscription::RdsRouteConfigProviderHelper::addOnDemandUpdateCallback(
    std::function<void()> callback) {
  // If RouteConfiguration has been initialized, run the callback to continue in filter chain,
  // otherwise cache it and wait for the route table to be initialized. If RouteConfiguration hasn't
  // been initialized, routeConfig() return a shared_ptr to NullConfigImpl. The name of
  // NullConfigImpl is an empty string.
  if (route_provider_ != nullptr && !routeConfig()->name().empty()) {
    callback();
    return;
  }
  on_demand_update_callbacks_.push_back(callback);
  // Initialize the rds provider if it has not been initialized. There is potential race here
  // because other worker threads may also post callback to on demand update the RouteConfiguration
  // associated with this scope. If rds provider has been initialized, just wait for
  // RouteConfiguration to be updated.
  maybeInitRdsConfigProvider();
}

void ScopedRdsConfigSubscription::RdsRouteConfigProviderHelper::runOnDemandUpdateCallback() {
  for (auto& callback : on_demand_update_callbacks_) {
    callback();
  }
  on_demand_update_callbacks_.clear();
}

void ScopedRdsConfigSubscription::RdsRouteConfigProviderHelper::initRdsConfigProvider(
    envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    Init::Manager& init_manager) {
  route_provider_ = std::dynamic_pointer_cast<RdsRouteConfigProviderImpl>(
      parent_.route_config_provider_manager_.createRdsRouteConfigProvider(
          rds, parent_.factory_context_, parent_.stat_prefix_, init_manager));

  rds_update_callback_handle_ = route_provider_->subscription().addUpdateCallback([this]() {
    // Subscribe to RDS update.
    parent_.onRdsConfigUpdate(scope_name_, route_provider_->configCast());
    return absl::OkStatus();
  });
  parent_.stats_.active_scopes_.inc();
}

void ScopedRdsConfigSubscription::RdsRouteConfigProviderHelper::maybeInitRdsConfigProvider() {
  // If the route provider have been initialized, return and wait for rds config update.
  if (route_provider_ != nullptr) {
    return;
  }

  // Create a init_manager to create a rds provider.
  // No transitive warming dependency here because only on demand update reach this point.
  Init::ManagerImpl srds_init_mgr("SRDS on demand init manager.");
  Cleanup srds_initialization_continuation([this, &srds_init_mgr] {
    Init::WatcherImpl noop_watcher(
        fmt::format("SRDS on demand ConfigUpdate watcher: {}", scope_name_),
        []() { /*Do nothing.*/ });
    srds_init_mgr.initialize(noop_watcher);
  });
  // Create route provider.
  envoy::extensions::filters::network::http_connection_manager::v3::Rds rds;
  rds.mutable_config_source()->MergeFrom(parent_.rds_config_source_);
  rds.set_route_config_name(
      parent_.scoped_route_map_[scope_name_]->configProto().route_configuration_name());
  initRdsConfigProvider(rds, srds_init_mgr);
  ENVOY_LOG(debug, fmt::format("Scope on demand update: {}", scope_name_));
  // If RouteConfiguration hasn't been initialized, routeConfig() return a shared_ptr to
  // NullConfigImpl. The name of NullConfigImpl is an empty string.
  if (routeConfig()->name().empty()) {
    return;
  }
  // If RouteConfiguration has been initialized, apply update to all the threads.
  parent_.onRdsConfigUpdate(scope_name_, route_provider_->configCast());
}

absl::StatusOr<bool> ScopedRdsConfigSubscription::addOrUpdateScopes(
    const std::vector<Envoy::Config::DecodedResourceRef>& resources, Init::Manager& init_manager,
    const std::string& version_info) {
  bool any_applied = false;
  envoy::extensions::filters::network::http_connection_manager::v3::Rds rds;
  rds.mutable_config_source()->MergeFrom(rds_config_source_);
  std::vector<ScopedRouteInfoConstSharedPtr> updated_scopes;
  for (const auto& resource : resources) {
    // Explicit copy so that we can std::move later.
    envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config =
        dynamic_cast<const envoy::config::route::v3::ScopedRouteConfiguration&>(
            resource.get().resource());
    if (scoped_route_config.route_configuration_name().empty()) {
      return absl::InvalidArgumentError("route_configuration_name is empty.");
    }
    const std::string scope_name = scoped_route_config.name();
    if (const auto& scope_info_iter = scoped_route_map_.find(scope_name);
        scope_info_iter != scoped_route_map_.end() &&
        scope_info_iter->second->configHash() == MessageUtil::hash(scoped_route_config)) {
      continue;
    }
    rds.set_route_config_name(scoped_route_config.route_configuration_name());
    std::unique_ptr<RdsRouteConfigProviderHelper> rds_config_provider_helper;
    std::shared_ptr<ScopedRouteInfo> scoped_route_info = nullptr;
    if (scoped_route_config.on_demand() == false) {
      // For default scopes, create a rds helper with rds provider initialized.
      rds_config_provider_helper =
          std::make_unique<RdsRouteConfigProviderHelper>(*this, scope_name, rds, init_manager);
      scoped_route_info = std::make_shared<ScopedRouteInfo>(
          std::move(scoped_route_config), rds_config_provider_helper->routeConfig());
    } else {
      // For on demand scopes, create a rds helper with rds provider uninitialized.
      rds_config_provider_helper =
          std::make_unique<RdsRouteConfigProviderHelper>(*this, scope_name);
      // scope_route_info->routeConfig() will be nullptr, because RouteConfiguration is not loaded.
      scoped_route_info =
          std::make_shared<ScopedRouteInfo>(std::move(scoped_route_config), nullptr);
    }
    route_provider_by_scope_[scope_name] = std::move(rds_config_provider_helper);
    scope_name_by_hash_[scoped_route_info->scopeKey().hash()] = scoped_route_info->scopeName();
    scoped_route_map_[scoped_route_info->scopeName()] = scoped_route_info;
    updated_scopes.push_back(scoped_route_info);
    any_applied = true;
    ENVOY_LOG(debug, "srds: queueing add/update of scoped_route '{}', version: {}",
              scoped_route_info->scopeName(), version_info);
  }

  // scoped_route_info of both eager loading and on demand scopes will be propagated to work
  // threads. Upon a scoped RouteConfiguration miss, if the scope exists, an on demand update
  // callback will be posted to main thread.
  if (!updated_scopes.empty()) {
    applyConfigUpdate([updated_scopes](ConfigProvider::ConfigConstSharedPtr config)
                          -> ConfigProvider::ConfigConstSharedPtr {
      auto* thread_local_scoped_config =
          const_cast<ScopedConfigImpl*>(static_cast<const ScopedConfigImpl*>(config.get()));
      thread_local_scoped_config->addOrUpdateRoutingScopes(updated_scopes);
      return config;
    });
  }
  return any_applied;
}

std::list<ScopedRdsConfigSubscription::RdsRouteConfigProviderHelperPtr>
ScopedRdsConfigSubscription::removeScopes(
    const Protobuf::RepeatedPtrField<std::string>& scope_names, const std::string& version_info) {
  std::list<ScopedRdsConfigSubscription::RdsRouteConfigProviderHelperPtr>
      to_be_removed_rds_providers;
  std::vector<std::string> removed_scope_names;
  for (const auto& scope_name : scope_names) {
    auto iter = scoped_route_map_.find(scope_name);
    if (iter != scoped_route_map_.end()) {
      auto rds_config_provider_helper_iter = route_provider_by_scope_.find(scope_name);
      if (rds_config_provider_helper_iter != route_provider_by_scope_.end()) {
        to_be_removed_rds_providers.emplace_back(
            std::move(rds_config_provider_helper_iter->second));
        route_provider_by_scope_.erase(rds_config_provider_helper_iter);
      }
      ASSERT(scope_name_by_hash_.find(iter->second->scopeKey().hash()) !=
             scope_name_by_hash_.end());
      scope_name_by_hash_.erase(iter->second->scopeKey().hash());
      scoped_route_map_.erase(iter);
      removed_scope_names.push_back(scope_name);
      ENVOY_LOG(debug, "srds: queueing removal of scoped route '{}', version: {}", scope_name,
                version_info);
    }
  }
  if (!removed_scope_names.empty()) {
    applyConfigUpdate([removed_scope_names](ConfigProvider::ConfigConstSharedPtr config)
                          -> ConfigProvider::ConfigConstSharedPtr {
      auto* thread_local_scoped_config =
          const_cast<ScopedConfigImpl*>(static_cast<const ScopedConfigImpl*>(config.get()));
      thread_local_scoped_config->removeRoutingScopes(removed_scope_names);
      return config;
    });
  }
  return to_be_removed_rds_providers;
}

absl::Status ScopedRdsConfigSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  // NOTE: deletes are done before adds/updates.
  absl::flat_hash_map<std::string, ScopedRouteInfoConstSharedPtr> to_be_removed_scopes;
  // Destruction of resume_rds will lift the floodgate for new RDS subscriptions.
  // Note in the case of partial acceptance, accepted RDS subscriptions should be started
  // despite of any error.
  ScopedResume resume_rds;
  // If new route config sources come after the local init manager's initialize() been
  // called, the init manager can't accept new targets. Instead we use a local override which will
  // start new subscriptions but not wait on them to be ready.
  std::unique_ptr<Init::ManagerImpl> srds_init_mgr;
  // NOTE: This should be defined after srds_init_mgr and resume_rds, as it depends on the
  // srds_init_mgr, and we want a single RDS discovery request to be sent to management
  // server.
  std::unique_ptr<Cleanup> srds_initialization_continuation;
  ASSERT(localInitManager().state() > Init::Manager::State::Uninitialized);
  const auto type_url = Envoy::Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>();
  // Pause RDS to not send a burst of RDS requests until we start all the new subscriptions.
  // In the case that localInitManager is uninitialized, RDS is already paused
  // either by Server init or LDS init.
  if (factory_context_.clusterManager().adsMux()) {
    resume_rds = factory_context_.clusterManager().adsMux()->pause(type_url);
  }
  // if local init manager is initialized, the parent init manager may have gone away.
  if (localInitManager().state() == Init::Manager::State::Initialized) {
    srds_init_mgr =
        std::make_unique<Init::ManagerImpl>(fmt::format("SRDS {}:{}", name_, version_info));
    srds_initialization_continuation =
        std::make_unique<Cleanup>([this, &srds_init_mgr, version_info] {
          // For new RDS subscriptions created after listener warming up, we don't wait for them to
          // warm up.
          Init::WatcherImpl noop_watcher(
              // Note: we just throw it away.
              fmt::format("SRDS ConfigUpdate watcher {}:{}", name_, version_info),
              []() { /*Do nothing.*/ });
          srds_init_mgr->initialize(noop_watcher);
        });
  }

  std::string exception_msg;
  Protobuf::RepeatedPtrField<std::string> clean_removed_resources =
      detectUpdateConflictAndCleanupRemoved(added_resources, removed_resources, exception_msg);
  if (!exception_msg.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("Error adding/updating scoped route(s): {}", exception_msg));
  }

  // Do not delete RDS config providers just yet, in case the to be deleted RDS subscriptions could
  // be reused by some to be added scopes.
  std::list<ScopedRdsConfigSubscription::RdsRouteConfigProviderHelperPtr>
      to_be_removed_rds_providers = removeScopes(clean_removed_resources, version_info);

  auto status_or_applied = addOrUpdateScopes(
      added_resources, (srds_init_mgr == nullptr ? localInitManager() : *srds_init_mgr),
      version_info);
  if (!status_or_applied.status().ok()) {
    return status_or_applied.status();
  }
  const bool any_applied = status_or_applied.value();
  const auto status = ConfigSubscriptionCommonBase::onConfigUpdate();
  if (!status.ok()) {
    return status;
  }
  if (any_applied || !to_be_removed_rds_providers.empty()) {
    setLastConfigInfo(absl::optional<LastConfigInfo>({absl::nullopt, version_info}));
  }
  stats_.all_scopes_.set(scoped_route_map_.size());
  stats_.config_reload_.inc();
  stats_.config_reload_time_ms_.set(DateUtil::nowToMilliseconds(factory_context_.timeSource()));
  return absl::OkStatus();
}

void ScopedRdsConfigSubscription::onRdsConfigUpdate(const std::string& scope_name,
                                                    ConfigConstSharedPtr new_rds_config) {
  auto iter = scoped_route_map_.find(scope_name);
  ASSERT(iter != scoped_route_map_.end(),
         fmt::format("trying to update route config for non-existing scope {}", scope_name));
  auto new_scoped_route_info = std::make_shared<ScopedRouteInfo>(
      envoy::config::route::v3::ScopedRouteConfiguration(iter->second->configProto()),
      std::move(new_rds_config));
  scoped_route_map_[new_scoped_route_info->scopeName()] = new_scoped_route_info;
  applyConfigUpdate([new_scoped_route_info](ConfigProvider::ConfigConstSharedPtr config)
                        -> ConfigProvider::ConfigConstSharedPtr {
    auto* thread_local_scoped_config =
        const_cast<ScopedConfigImpl*>(static_cast<const ScopedConfigImpl*>(config.get()));
    thread_local_scoped_config->addOrUpdateRoutingScopes({new_scoped_route_info});
    return config;
  });
  // The data plane may wait for the route configuration to come back.
  route_provider_by_scope_[scope_name]->runOnDemandUpdateCallback();
}

// TODO(stevenzzzz): see issue #7508, consider generalizing this function as it overlaps with
// CdsApiImpl::onConfigUpdate.
absl::Status ScopedRdsConfigSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& resources,
    const std::string& version_info) {
  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  for (const auto& scoped_route : scoped_route_map_) {
    *to_remove_repeated.Add() = scoped_route.first;
  }
  return onConfigUpdate(resources, to_remove_repeated, version_info);
}

Protobuf::RepeatedPtrField<std::string>
ScopedRdsConfigSubscription::detectUpdateConflictAndCleanupRemoved(
    const std::vector<Envoy::Config::DecodedResourceRef>& resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, std::string& exception_msg) {
  Protobuf::RepeatedPtrField<std::string> clean_removed_resources;
  // All the scope names to be removed or updated.
  absl::flat_hash_set<std::string> updated_or_removed_scopes;
  for (const std::string& removed_resource : removed_resources) {
    updated_or_removed_scopes.insert(removed_resource);
  }
  for (const auto& resource : resources) {
    const auto& scoped_route =
        dynamic_cast<const envoy::config::route::v3::ScopedRouteConfiguration&>(
            resource.get().resource());
    updated_or_removed_scopes.insert(scoped_route.name());
  }

  absl::flat_hash_map<uint64_t, std::string> scope_name_by_hash = scope_name_by_hash_;
  absl::erase_if(scope_name_by_hash, [&updated_or_removed_scopes](const auto& key_name) {
    auto const& [key, name] = key_name;
    UNREFERENCED_PARAMETER(key);
    return updated_or_removed_scopes.contains(name);
  });
  absl::flat_hash_map<std::string, envoy::config::route::v3::ScopedRouteConfiguration>
      scoped_routes;
  for (const auto& resource : resources) {
    // Throws (thus rejects all) on any error.
    const auto& scoped_route =
        dynamic_cast<const envoy::config::route::v3::ScopedRouteConfiguration&>(
            resource.get().resource());
    const std::string& scope_name = scoped_route.name();
    auto scope_config_inserted = scoped_routes.try_emplace(scope_name, std::move(scoped_route));
    if (!scope_config_inserted.second) {
      exception_msg = fmt::format("duplicate scoped route configuration '{}' found", scope_name);
      return clean_removed_resources;
    }
    envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config =
        scope_config_inserted.first->second;
    const uint64_t key_fingerprint =
        ScopedRouteInfo(std::move(scoped_route_config), nullptr).scopeKey().hash();
    if (!scope_name_by_hash.try_emplace(key_fingerprint, scope_name).second) {
      exception_msg =
          fmt::format("scope key conflict found, first scope is '{}', second scope is '{}'",
                      scope_name_by_hash[key_fingerprint], scope_name);
      return clean_removed_resources;
    }
  }

  // only remove resources that is not going to be updated.
  for (const std::string& removed_resource : removed_resources) {
    if (!scoped_routes.contains(removed_resource)) {
      *clean_removed_resources.Add() = removed_resource;
    }
  }
  return clean_removed_resources;
}

void ScopedRdsConfigSubscription::onDemandRdsUpdate(
    std::shared_ptr<Router::ScopeKey> scope_key, Event::Dispatcher& thread_local_dispatcher,
    Http::RouteConfigUpdatedCallback&& route_config_updated_cb,
    std::weak_ptr<Envoy::Config::ConfigSubscriptionCommonBase> weak_subscription) {
  factory_context_.mainThreadDispatcher().post([this, &thread_local_dispatcher, scope_key,
                                                route_config_updated_cb, weak_subscription]() {
    // If the subscription has been destroyed, return immediately.
    if (!weak_subscription.lock()) {
      thread_local_dispatcher.post([route_config_updated_cb] { route_config_updated_cb(false); });
      return;
    }

    auto iter = scope_name_by_hash_.find(scope_key->hash());
    // Return to filter chain if we can't find the scope.
    // The scope may have been destroyed when callback reach the main thread.
    if (iter == scope_name_by_hash_.end()) {
      thread_local_dispatcher.post([route_config_updated_cb] { route_config_updated_cb(false); });
      return;
    }
    // Wrap the thread local dispatcher inside the callback.
    std::function<void()> thread_local_updated_callback = [route_config_updated_cb,
                                                           &thread_local_dispatcher]() {
      thread_local_dispatcher.post([route_config_updated_cb] { route_config_updated_cb(true); });
    };
    std::string scope_name = iter->second;
    // On demand initialization inside main thread.
    route_provider_by_scope_[scope_name]->addOnDemandUpdateCallback(thread_local_updated_callback);
  });
}

ScopedRdsConfigProvider::ScopedRdsConfigProvider(
    ScopedRdsConfigSubscriptionSharedPtr&& subscription)
    : MutableConfigProviderCommonBase(std::move(subscription), ConfigProvider::ApiType::Delta) {}

ProtobufTypes::MessagePtr
ScopedRoutesConfigProviderManager::dumpConfigs(const Matchers::StringMatcher& name_matcher) const {
  auto config_dump = std::make_unique<envoy::admin::v3::ScopedRoutesConfigDump>();
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
        if (!name_matcher.match(it.second->configProto().name())) {
          continue;
        }
        dynamic_config->mutable_scoped_route_configs()->Add()->PackFrom(it.second->configProto());
      }
      TimestampUtil::systemClockToTimestamp(subscription->lastUpdated(),
                                            *dynamic_config->mutable_last_updated());
    }
  }

  for (const auto& provider : immutableConfigProviders(ConfigProviderInstanceType::Inline)) {
    const auto protos_info =
        provider->configProtoInfoVector<envoy::config::route::v3::ScopedRouteConfiguration>();
    ASSERT(protos_info != absl::nullopt);
    auto* inline_config = config_dump->mutable_inline_scoped_route_configs()->Add();
    inline_config->set_name(static_cast<InlineScopedRoutesConfigProvider*>(provider)->name());
    for (const auto& config_proto : protos_info.value().config_protos_) {
      if (!name_matcher.match(config_proto->name())) {
        continue;
      }
      inline_config->mutable_scoped_route_configs()->Add()->PackFrom(*config_proto);
    }
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *inline_config->mutable_last_updated());
  }

  return config_dump;
}

ConfigProviderPtr ScopedRoutesConfigProviderManager::createXdsConfigProvider(
    const Protobuf::Message& config_source_proto,
    Server::Configuration::ServerFactoryContext& factory_context, Init::Manager& init_manager,
    const std::string& stat_prefix, const ConfigProviderManager::OptionalArg& optarg) {
  const auto& typed_optarg = static_cast<const ScopedRoutesConfigProviderManagerOptArg&>(optarg);
  ScopedRdsConfigSubscriptionSharedPtr subscription =
      ConfigProviderManagerImplBase::getSubscription<ScopedRdsConfigSubscription>(
          config_source_proto, init_manager,
          [&config_source_proto, &factory_context, &stat_prefix,
           &typed_optarg](const uint64_t manager_identifier,
                          ConfigProviderManagerImplBase& config_provider_manager)
              -> Envoy::Config::ConfigSubscriptionCommonBaseSharedPtr {
            const auto& scoped_rds_config_source = dynamic_cast<
                const envoy::extensions::filters::network::http_connection_manager::v3::ScopedRds&>(
                config_source_proto);
            return std::make_shared<ScopedRdsConfigSubscription>(
                scoped_rds_config_source, manager_identifier, typed_optarg.scoped_routes_name_,
                factory_context, stat_prefix, typed_optarg.rds_config_source_,
                static_cast<ScopedRoutesConfigProviderManager&>(config_provider_manager)
                    .routeConfigProviderManager(),
                static_cast<ScopedRoutesConfigProviderManager&>(config_provider_manager));
          });

  return std::make_unique<ScopedRdsConfigProvider>(std::move(subscription));
}

ConfigProviderPtr ScopedRoutesConfigProviderManager::createStaticConfigProvider(
    ProtobufTypes::ConstMessagePtrVector&& config_protos,
    Server::Configuration::ServerFactoryContext& factory_context,
    const ConfigProviderManager::OptionalArg& optarg) {
  const auto& typed_optarg = static_cast<const ScopedRoutesConfigProviderManagerOptArg&>(optarg);
  return std::make_unique<InlineScopedRoutesConfigProvider>(
      std::move(config_protos), typed_optarg.scoped_routes_name_, factory_context, *this,
      typed_optarg.rds_config_source_);
}

REGISTER_FACTORY(SrdsFactoryDefault, SrdsFactory);

} // namespace Router
} // namespace Envoy
