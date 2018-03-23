#include "common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/v2/rds.pb.validate.h"
#include "envoy/api/v2/route/route.pb.validate.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/rds_json.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"
#include "common/router/rds_subscription.h"

namespace Envoy {
namespace Router {

RouteConfigProviderSharedPtr RouteConfigProviderUtil::create(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        config,
    Runtime::Loader& runtime, Upstream::ClusterManager& cm, Stats::Scope& scope,
    const std::string& stat_prefix, Init::Manager& init_manager,
    RouteConfigProviderManager& route_config_provider_manager) {
  switch (config.route_specifier_case()) {
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      kRouteConfig:
    return RouteConfigProviderSharedPtr{
        new StaticRouteConfigProviderImpl(config.route_config(), runtime, cm)};
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::kRds:
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

// TODO(htuch): If support for multiple clusters is added per #1170 cluster_name_
// initialization needs to be fixed.
RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    const std::string& manager_identifier, Runtime::Loader& runtime, Upstream::ClusterManager& cm,
    Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, const std::string& stat_prefix,
    ThreadLocal::SlotAllocator& tls, RouteConfigProviderManagerImpl& route_config_provider_manager)
    : runtime_(runtime), cm_(cm), tls_(tls.allocateSlot()),
      route_config_name_(rds.route_config_name()),
      scope_(scope.createScope(stat_prefix + "rds." + route_config_name_ + ".")),
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
  config_source_ = MessageUtil::getJsonStringFromMessage(rds.config_source(), true);
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
  if (resources.empty()) {
    ENVOY_LOG(debug, "Missing RouteConfiguration for {} in onConfigUpdate()", route_config_name_);
    stats_.update_empty_.inc();
    runInitializeCallbackIfAny();
    return;
  }
  if (resources.size() != 1) {
    throw EnvoyException(fmt::format("Unexpected RDS resource length: {}", resources.size()));
  }
  const auto& route_config = resources[0];
  MessageUtil::validate(route_config);
  // TODO(PiotrSikora): Remove this hack once fixed internally.
  if (!(route_config.name() == route_config_name_)) {
    throw EnvoyException(fmt::format("Unexpected RDS configuration (expecting {}): {}",
                                     route_config_name_, route_config.name()));
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
    route_config_proto_ = route_config;
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
    const LocalInfo::LocalInfo& local_info, ThreadLocal::SlotAllocator& tls, Server::Admin& admin)
    : runtime_(runtime), dispatcher_(dispatcher), random_(random), local_info_(local_info),
      tls_(tls), admin_(admin) {
  admin_.addHandler("/routes", "print out currently loaded dynamic HTTP route tables",
                    MAKE_ADMIN_HANDLER(RouteConfigProviderManagerImpl::handlerRoutes), true, false);
}

RouteConfigProviderManagerImpl::~RouteConfigProviderManagerImpl() {
  admin_.removeHandler("/routes");
}

std::vector<RdsRouteConfigProviderSharedPtr>
RouteConfigProviderManagerImpl::rdsRouteConfigProviders() {
  std::vector<RdsRouteConfigProviderSharedPtr> ret;
  ret.reserve(route_config_providers_.size());
  for (const auto& element : route_config_providers_) {
    // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
    // in the RdsRouteConfigProviderImpl destructor, and the single threaded nature
    // of this code, locking the weak_ptr will not fail.
    RdsRouteConfigProviderSharedPtr provider = element.second.lock();
    ASSERT(provider)
    ret.push_back(provider);
  }
  return ret;
};

Router::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::getRouteConfigProvider(
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    Upstream::ClusterManager& cm, Stats::Scope& scope, const std::string& stat_prefix,
    Init::Manager& init_manager) {

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

Http::Code RouteConfigProviderManagerImpl::handlerRoutes(absl::string_view url, Http::HeaderMap&,
                                                         Buffer::Instance& response) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  // If there are no query params, print out all the configured route tables.
  if (query_params.size() == 0) {
    return handlerRoutesLoop(response, rdsRouteConfigProviders());
  }

  // If there are query params, make sure it is only the route_config_name param.
  const auto it = query_params.find("route_config_name");
  if (query_params.size() == 1 && it != query_params.end()) {
    // Create a vector with all the providers that have the queried route_config_name.
    std::vector<RdsRouteConfigProviderSharedPtr> selected_providers;
    for (const auto& provider : rdsRouteConfigProviders()) {
      if (provider->routeConfigName() == it->second) {
        selected_providers.push_back(provider);
      }
    }
    return handlerRoutesLoop(response, selected_providers);
  }
  response.add("{\n");
  response.add("    \"general_usage\": \"/routes (dump all dynamic HTTP route tables).\",\n");
  response.add("    \"specify_name_usage\": \"/routes?route_config_name=<name> (dump all dynamic "
               "HTTP route tables with the "
               "<name> if any).\"\n");
  response.add("}");
  return Http::Code::NotFound;
}

Http::Code RouteConfigProviderManagerImpl::handlerRoutesLoop(
    Buffer::Instance& response, const std::vector<RdsRouteConfigProviderSharedPtr> providers) {
  bool first_item = true;
  response.add("[\n");
  for (const auto& provider : providers) {
    if (!first_item) {
      response.add(",");
    } else {
      first_item = false;
    }
    response.add("{\n");
    response.add(fmt::format("\"version_info\": \"{}\",\n", provider->versionInfo()));
    response.add(fmt::format("\"route_config_name\": \"{}\",\n", provider->routeConfigName()));
    response.add(fmt::format("\"config_source\": {},\n", provider->configSource()));
    response.add("\"route_table_dump\": ");
    response.add(fmt::format("{}\n", provider->configAsJson()));
    response.add("}\n");
  }
  response.add("]\n");
  return Http::Code::OK;
}
} // namespace Router
} // namespace Envoy
