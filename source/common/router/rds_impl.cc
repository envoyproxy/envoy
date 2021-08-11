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
#include "source/common/config/version_converter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {

RouteConfigProviderSharedPtr RouteConfigProviderUtil::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator, Init::Manager& init_manager,
    const std::string& stat_prefix, RouteConfigProviderManager& route_config_provider_manager) {
  OptionalHttpFilters optional_http_filters;
  auto& filters = config.http_filters();
  for (const auto& filter : filters) {
    if (filter.is_optional()) {
      optional_http_filters.insert(filter.name());
    }
  }
  switch (config.route_specifier_case()) {
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::kRouteConfig:
    return route_config_provider_manager.createStaticRouteConfigProvider(
        config.route_config(), optional_http_filters, factory_context, validator);
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::kRds:
    return route_config_provider_manager.createRdsRouteConfigProvider(
        // At the creation of a RDS route config provider, the factory_context's initManager is
        // always valid, though the init manager may go away later when the listener goes away.
        config.rds(), optional_http_filters, factory_context, stat_prefix, init_manager);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::config::route::v3::RouteConfiguration& config,
    const OptionalHttpFilters& optional_http_filters,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator,
    RouteConfigProviderManagerImpl& route_config_provider_manager)
    : config_(new ConfigImpl(config, optional_http_filters, factory_context, validator, true)),
      route_config_proto_{config}, last_updated_(factory_context.timeSource().systemTime()),
      route_config_provider_manager_(route_config_provider_manager) {
  route_config_provider_manager_.static_route_config_providers_.insert(this);
}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.static_route_config_providers_.erase(this);
}

// TODO(htuch): If support for multiple clusters is added per #1170 cluster_name_
RdsRouteConfigSubscription::RdsRouteConfigSubscription(
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    const uint64_t manager_identifier, Server::Configuration::ServerFactoryContext& factory_context,
    const std::string& stat_prefix, const OptionalHttpFilters& optional_http_filters,
    Envoy::Router::RouteConfigProviderManagerImpl& route_config_provider_manager)
    : Envoy::Config::SubscriptionBase<envoy::config::route::v3::RouteConfiguration>(
          rds.config_source().resource_api_version(),
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      route_config_name_(rds.route_config_name()),
      scope_(factory_context.scope().createScope(stat_prefix + "rds." + route_config_name_ + ".")),
      factory_context_(factory_context),
      parent_init_target_(fmt::format("RdsRouteConfigSubscription init {}", route_config_name_),
                          [this]() { local_init_manager_.initialize(local_init_watcher_); }),
      local_init_watcher_(fmt::format("RDS local-init-watcher {}", rds.route_config_name()),
                          [this]() { parent_init_target_.ready(); }),
      local_init_target_(
          fmt::format("RdsRouteConfigSubscription local-init-target {}", route_config_name_),
          [this]() { subscription_->start({route_config_name_}); }),
      local_init_manager_(fmt::format("RDS local-init-manager {}", route_config_name_)),
      stat_prefix_(stat_prefix),
      stats_({ALL_RDS_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))}),
      route_config_provider_manager_(route_config_provider_manager),
      manager_identifier_(manager_identifier), optional_http_filters_(optional_http_filters) {
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          rds.config_source(), Grpc::Common::typeUrl(resource_name), *scope_, *this,
          resource_decoder_, {});
  local_init_manager_.add(local_init_target_);
  config_update_info_ =
      std::make_unique<RouteConfigUpdateReceiverImpl>(factory_context, optional_http_filters_);
}

RdsRouteConfigSubscription::~RdsRouteConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  local_init_target_.ready();

  // The ownership of RdsRouteConfigProviderImpl is shared among all HttpConnectionManagers that
  // hold a shared_ptr to it. The RouteConfigProviderManager holds weak_ptrs to the
  // RdsRouteConfigProviders. Therefore, the map entry for the RdsRouteConfigProvider has to get
  // cleaned by the RdsRouteConfigProvider's destructor.
  route_config_provider_manager_.dynamic_route_config_providers_.erase(manager_identifier_);
}

void RdsRouteConfigSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& resources,
    const std::string& version_info) {
  if (!validateUpdateSize(resources.size())) {
    return;
  }
  const auto& route_config = dynamic_cast<const envoy::config::route::v3::RouteConfiguration&>(
      resources[0].get().resource());
  if (route_config.name() != route_config_name_) {
    throw EnvoyException(fmt::format("Unexpected RDS configuration (expecting {}): {}",
                                     route_config_name_, route_config.name()));
  }
  if (route_config_provider_opt_.has_value()) {
    route_config_provider_opt_.value()->validateConfig(route_config);
  }
  std::unique_ptr<Init::ManagerImpl> noop_init_manager;
  std::unique_ptr<Cleanup> resume_rds;
  if (config_update_info_->onRdsUpdate(route_config, version_info)) {
    stats_.config_reload_.inc();
    stats_.config_reload_time_ms_.set(DateUtil::nowToMilliseconds(factory_context_.timeSource()));
    if (config_update_info_->protobufConfiguration().has_vhds() &&
        config_update_info_->vhdsConfigurationChanged()) {
      ENVOY_LOG(
          debug,
          "rds: vhds configuration present/changed, (re)starting vhds: config_name={} hash={}",
          route_config_name_, config_update_info_->configHash());
      maybeCreateInitManager(version_info, noop_init_manager, resume_rds);
      vhds_subscription_ = std::make_unique<VhdsSubscription>(
          config_update_info_, factory_context_, stat_prefix_, route_config_provider_opt_,
          config_update_info_->protobufConfiguration()
              .vhds()
              .config_source()
              .resource_api_version());
      vhds_subscription_->registerInitTargetWithInitManager(
          noop_init_manager == nullptr ? local_init_manager_ : *noop_init_manager);
    }

    ENVOY_LOG(debug, "rds: loading new configuration: config_name={} hash={}", route_config_name_,
              config_update_info_->configHash());

    if (route_config_provider_opt_.has_value()) {
      route_config_provider_opt_.value()->onConfigUpdate();
    }
    // RDS update removed VHDS configuration
    if (!config_update_info_->protobufConfiguration().has_vhds()) {
      vhds_subscription_.release();
    }

    update_callback_manager_.runCallbacks();
  }

  local_init_target_.ready();
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

void RdsRouteConfigSubscription::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    // TODO(#2500) when on-demand resource loading is supported, an RDS removal may make sense
    // (see discussion in #6879), and so we should do something other than ignoring here.
    ENVOY_LOG(
        error,
        "Server sent a delta RDS update attempting to remove a resource (name: {}). Ignoring.",
        removed_resources[0]);
  }
  if (!added_resources.empty()) {
    onConfigUpdate(added_resources, added_resources[0].get().version());
  }
}

void RdsRouteConfigSubscription::onConfigUpdateFailed(
    Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  local_init_target_.ready();
}

void RdsRouteConfigSubscription::updateOnDemand(const std::string& aliases) {
  if (vhds_subscription_.get() == nullptr) {
    return;
  }
  vhds_subscription_->updateOnDemand(aliases);
}

bool RdsRouteConfigSubscription::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    ENVOY_LOG(debug, "Missing RouteConfiguration for {} in onConfigUpdate()", route_config_name_);
    stats_.update_empty_.inc();
    local_init_target_.ready();
    return false;
  }
  if (num_resources != 1) {
    throw EnvoyException(fmt::format("Unexpected RDS resource length: {}", num_resources));
    // (would be a return false here)
  }
  return true;
}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    RdsRouteConfigSubscriptionSharedPtr&& subscription,
    Server::Configuration::ServerFactoryContext& factory_context,
    const OptionalHttpFilters& optional_http_filters)
    : subscription_(std::move(subscription)),
      config_update_info_(subscription_->routeConfigUpdate()), factory_context_(factory_context),
      validator_(factory_context.messageValidationContext().dynamicValidationVisitor()),
      tls_(factory_context.threadLocal()), optional_http_filters_(optional_http_filters) {
  ConfigConstSharedPtr initial_config;
  if (config_update_info_->configInfo().has_value()) {
    initial_config =
        std::make_shared<ConfigImpl>(config_update_info_->protobufConfiguration(),
                                     optional_http_filters_, factory_context_, validator_, false);
  } else {
    initial_config = std::make_shared<NullConfigImpl>();
  }
  tls_.set([initial_config](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalConfig>(initial_config);
  });
  // It should be 1:1 mapping due to shared rds config.
  ASSERT(!subscription_->routeConfigProvider().has_value());
  subscription_->routeConfigProvider().emplace(this);
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  ASSERT(subscription_->routeConfigProvider().has_value());
  subscription_->routeConfigProvider().reset();
}

Router::ConfigConstSharedPtr RdsRouteConfigProviderImpl::config() { return tls_->config_; }

void RdsRouteConfigProviderImpl::onConfigUpdate() {
  tls_.runOnAllThreads([new_config = config_update_info_->parsedConfiguration()](
                           OptRef<ThreadLocalConfig> tls) { tls->config_ = new_config; });

  const auto aliases = config_update_info_->resourceIdsInLastVhdsUpdate();
  // Regular (non-VHDS) RDS updates don't populate aliases fields in resources.
  if (aliases.empty()) {
    return;
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
}

void RdsRouteConfigProviderImpl::validateConfig(
    const envoy::config::route::v3::RouteConfiguration& config) const {
  // TODO(lizan): consider cache the config here until onConfigUpdate.
  ConfigImpl validation_config(config, optional_http_filters_, factory_context_, validator_, false);
}

// Schedules a VHDS request on the main thread and queues up the callback to use when the VHDS
// response has been propagated to the worker thread that was the request origin.
void RdsRouteConfigProviderImpl::requestVirtualHostsUpdate(
    const std::string& for_domain, Event::Dispatcher& thread_local_dispatcher,
    std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb) {
  auto alias =
      VhdsSubscription::domainNameToAlias(config_update_info_->routeConfigName(), for_domain);
  // The RdsRouteConfigProviderImpl instance can go away before the dispatcher has a chance to
  // execute the callback. still_alive shared_ptr will be deallocated when the current instance of
  // the RdsRouteConfigProviderImpl is deallocated; we rely on a weak_ptr to still_alive flag to
  // determine if the RdsRouteConfigProviderImpl instance is still valid.
  factory_context_.dispatcher().post([this, maybe_still_alive = std::weak_ptr<bool>(still_alive_),
                                      alias, &thread_local_dispatcher,
                                      route_config_updated_cb]() -> void {
    if (maybe_still_alive.lock()) {
      subscription_->updateOnDemand(alias);
      config_update_callbacks_.push_back({alias, thread_local_dispatcher, route_config_updated_cb});
    }
  });
}

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(Server::Admin& admin) {
  config_tracker_entry_ =
      admin.getConfigTracker().add("routes", [this](const Matchers::StringMatcher& matcher) {
        return dumpRouteConfigs(matcher);
      });
  // ConfigTracker keys must be unique. We are asserting that no one has stolen the "routes" key
  // from us, since the returned entry will be nullptr if the key already exists.
  RELEASE_ASSERT(config_tracker_entry_, "");
}

Router::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::createRdsRouteConfigProvider(
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    const OptionalHttpFilters& optional_http_filters,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager) {
  // RdsRouteConfigSubscriptions are unique based on their serialized RDS config.
  const uint64_t manager_identifier = MessageUtil::hash(rds);
  auto it = dynamic_route_config_providers_.find(manager_identifier);

  if (it == dynamic_route_config_providers_.end()) {
    // std::make_shared does not work for classes with private constructors. There are ways
    // around it. However, since this is not a performance critical path we err on the side
    // of simplicity.
    RdsRouteConfigSubscriptionSharedPtr subscription(new RdsRouteConfigSubscription(
        rds, manager_identifier, factory_context, stat_prefix, optional_http_filters, *this));
    init_manager.add(subscription->parent_init_target_);
    RdsRouteConfigProviderImplSharedPtr new_provider{new RdsRouteConfigProviderImpl(
        std::move(subscription), factory_context, optional_http_filters)};
    dynamic_route_config_providers_.insert(
        {manager_identifier, std::weak_ptr<RdsRouteConfigProviderImpl>(new_provider)});
    return new_provider;
  } else {
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    auto existing_provider = it->second.lock();
    RELEASE_ASSERT(existing_provider != nullptr,
                   absl::StrCat("cannot find subscribed rds resource ", rds.route_config_name()));
    init_manager.add(existing_provider->subscription_->parent_init_target_);
    return existing_provider;
  }
}

RouteConfigProviderPtr RouteConfigProviderManagerImpl::createStaticRouteConfigProvider(
    const envoy::config::route::v3::RouteConfiguration& route_config,
    const OptionalHttpFilters& optional_http_filters,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {
  auto provider = std::make_unique<StaticRouteConfigProviderImpl>(
      route_config, optional_http_filters, factory_context, validator, *this);
  static_route_config_providers_.insert(provider.get());
  return provider;
}

std::unique_ptr<envoy::admin::v3::RoutesConfigDump>
RouteConfigProviderManagerImpl::dumpRouteConfigs(
    const Matchers::StringMatcher& name_matcher) const {
  auto config_dump = std::make_unique<envoy::admin::v3::RoutesConfigDump>();

  for (const auto& element : dynamic_route_config_providers_) {
    const auto& subscription = element.second.lock()->subscription_;
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigSubscription destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    ASSERT(subscription);
    ASSERT(subscription->route_config_provider_opt_.has_value());

    if (subscription->routeConfigUpdate()->configInfo()) {
      if (!name_matcher.match(subscription->routeConfigUpdate()->protobufConfiguration().name())) {
        continue;
      }
      auto* dynamic_config = config_dump->mutable_dynamic_route_configs()->Add();
      dynamic_config->set_version_info(subscription->routeConfigUpdate()->configVersion());
      dynamic_config->mutable_route_config()->PackFrom(
          API_RECOVER_ORIGINAL(subscription->routeConfigUpdate()->protobufConfiguration()));
      TimestampUtil::systemClockToTimestamp(subscription->routeConfigUpdate()->lastUpdated(),
                                            *dynamic_config->mutable_last_updated());
    }
  }

  for (const auto& provider : static_route_config_providers_) {
    ASSERT(provider->configInfo());
    if (!name_matcher.match(provider->configInfo().value().config_.name())) {
      continue;
    }
    auto* static_config = config_dump->mutable_static_route_configs()->Add();
    static_config->mutable_route_config()->PackFrom(
        API_RECOVER_ORIGINAL(provider->configInfo().value().config_));
    TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                          *static_config->mutable_last_updated());
  }

  return config_dump;
}

} // namespace Router
} // namespace Envoy
