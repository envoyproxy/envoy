#include "common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/config/rds_json.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/router/config_impl.h"
#include "common/router/rds_subscription.h"

namespace Envoy {
namespace Router {

RouteConfigProviderSharedPtr RouteConfigProviderUtil::create(
    const envoy::api::v2::filter::HttpConnectionManager& config, Runtime::Loader& runtime,
    Upstream::ClusterManager& cm, Stats::Scope& scope, const std::string& stat_prefix,
    Init::Manager& init_manager, RouteConfigProviderManager& route_config_provider_manager) {
  switch (config.route_specifier_case()) {
  case envoy::api::v2::filter::HttpConnectionManager::kRouteConfig:
    return RouteConfigProviderSharedPtr{
        new StaticRouteConfigProviderImpl(config.route_config(), runtime, cm)};
  case envoy::api::v2::filter::HttpConnectionManager::kRds:
    return route_config_provider_manager.getRouteConfigProvider(config.rds(), cm, scope,
                                                                stat_prefix, init_manager);
  default:
    NOT_REACHED;
  }
}

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::api::v2::RouteConfiguration& config, Runtime::Loader& runtime,
    Upstream::ClusterManager& cm)
    : config_(new ConfigImpl(config, runtime, cm, true)) {}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    const envoy::api::v2::filter::Rds& rds, const std::string& manager_identifier,
    Runtime::Loader& runtime, Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
    Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
    const std::string& stat_prefix, ThreadLocal::SlotAllocator& tls,
    RouteConfigProviderManagerImpl& route_config_provider_manager)
    : runtime_(runtime), cm_(cm), tls_(tls.allocateSlot()),
      route_config_name_(rds.route_config_name()), scope_(scope.createScope(stat_prefix + "rds.")),
      stats_({ALL_RDS_STATS(POOL_COUNTER(*scope_))}),
      route_config_provider_manager_(route_config_provider_manager),
      manager_identifier_(manager_identifier) {
  ::Envoy::Config::Utility::checkLocalInfo("rds", local_info);
  ConfigConstSharedPtr initial_config(new NullConfigImpl());
  tls_->set([initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalConfig>(initial_config);
  });
  subscription_ = Envoy::Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::RouteConfiguration>(
      rds.config_source(), local_info.node(), dispatcher, cm, random, *scope_,
      [this, &rds, &dispatcher, &random,
       &local_info]() -> Envoy::Config::Subscription<envoy::api::v2::RouteConfiguration>* {
        return new RdsSubscription(Envoy::Config::Utility::generateStats(*scope_), rds, cm_,
                                   dispatcher, random, local_info);
      },
      "envoy.api.v2.RouteDiscoveryService.FetchRoutes",
      "envoy.api.v2.RouteDiscoveryService.StreamRoutes");
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  runInitializeCallbackIfAny();

  // The ownership of RdsRouteConfigProviderImpl is shared among all HttpConnectionManagers that
  // hold a shared_ptr to it. The RouteConfigProviderManager holds weak_ptrs to the
  // RdsRouteConfigProviders. Therefore, the map entry for the RdsRouteConfigProvider has to get
  // cleaned by the RdsRouteConfigProvider's destructor.
  route_config_provider_manager_.route_config_providers_.erase(manager_identifier_);
}

Router::ConfigConstSharedPtr RdsRouteConfigProviderImpl::config() {
  return tls_->getTyped<ThreadLocalConfig>().config_;
}

void RdsRouteConfigProviderImpl::onConfigUpdate(const ResourceVector& resources) {
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected RDS resource length: {}", resources.size()));
  }
  const auto& route_config = resources[0];
  if (ProtobufTypes::FromString(route_config.name()) != route_config_name_) {
    throw EnvoyException(fmt::format("Unexpected RDS configuration (expecting {}): {}",
                                     route_config_name_,
                                     ProtobufTypes::FromString(route_config.name())));
  }
  const uint64_t new_hash = MessageUtil::hash(route_config);
  if (new_hash != last_config_hash_ || !initialized_) {
    ConfigConstSharedPtr new_config(new ConfigImpl(route_config, runtime_, cm_, false));
    initialized_ = true;
    last_config_hash_ = new_hash;
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "rds: loading new configuration: config_name={} hash={}", route_config_name_,
              new_hash);
    tls_->runOnAllThreads(
        [this, new_config]() -> void { tls_->getTyped<ThreadLocalConfig>().config_ = new_config; });
  }
  runInitializeCallbackIfAny();
}

void RdsRouteConfigProviderImpl::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad
  // config.
  runInitializeCallbackIfAny();
}

void RdsRouteConfigProviderImpl::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

void RdsRouteConfigProviderImpl::registerInitTarget(Init::Manager& init_manager) {
  init_manager.registerTarget(*this);
}

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(
    Runtime::Loader& runtime, Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, ThreadLocal::SlotAllocator& tls)
    : runtime_(runtime), dispatcher_(dispatcher), random_(random), local_info_(local_info),
      tls_(tls) {}

std::vector<Router::RouteConfigProviderSharedPtr>
RouteConfigProviderManagerImpl::routeConfigProviders() {
  std::vector<Router::RouteConfigProviderSharedPtr> ret;
  ret.reserve(route_config_providers_.size());
  for (const auto& element : route_config_providers_) {
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigProviderImpl destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    Router::RouteConfigProviderSharedPtr provider = element.second.lock();
    ASSERT(provider)
    ret.push_back(provider);
  }
  return ret;
};

Router::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::getRouteConfigProvider(
    const envoy::api::v2::filter::Rds& rds, Upstream::ClusterManager& cm, Stats::Scope& scope,
    const std::string& stat_prefix, Init::Manager& init_manager) {

  // RdsRouteConfigProviders are unique based on their serialized RDS config.
  // TODO(htuch): Full serialization here gives large IDs, could get away with a
  // strong hash instead.
  const std::string manager_identifier = rds.SerializeAsString();

  auto it = route_config_providers_.find(manager_identifier);
  if (it == route_config_providers_.end()) {
    // std::make_shared does not work for classes with private constructors. There are ways
    // around it. However, since this is not a performance critical path we err on the side
    // of simplicity.
    std::shared_ptr<RdsRouteConfigProviderImpl> new_provider{
        new RdsRouteConfigProviderImpl(rds, manager_identifier, runtime_, cm, dispatcher_, random_,
                                       local_info_, scope, stat_prefix, tls_, *this)};

    new_provider->registerInitTarget(init_manager);

    route_config_providers_.insert({manager_identifier, new_provider});

    return new_provider;
  }

  // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
  // in the RdsRouteConfigProviderImpl destructor, and the single threaded nature
  // of this code, locking the weak_ptr will not fail.
  Router::RouteConfigProviderSharedPtr new_provider = it->second.lock();
  ASSERT(new_provider);
  return new_provider;
};

} // namespace Router
} // namespace Envoy
