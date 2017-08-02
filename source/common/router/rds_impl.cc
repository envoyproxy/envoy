#include "common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/config/utility.h"
#include "common/json/config_schemas.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

RouteConfigProviderSharedPtr
RouteConfigProviderUtil::create(const Json::Object& config, Runtime::Loader& runtime,
                                Upstream::ClusterManager& cm, Stats::Scope& scope,
                                const std::string& stat_prefix, Init::Manager& init_manager,
                                HttpRouteManager& http_route_manager) {
  bool has_rds = config.hasObject("rds");
  bool has_route_config = config.hasObject("route_config");
  if (!(has_rds ^ has_route_config)) {
    throw EnvoyException(
        "http connection manager must have either rds or route_config but not both");
  }

  if (has_route_config) {
    return RouteConfigProviderSharedPtr{
        new StaticRouteConfigProviderImpl(*config.getObject("route_config"), runtime, cm)};
  } else {
    Json::ObjectSharedPtr rds_config = config.getObject("rds");
    rds_config->validateSchema(Json::Schema::RDS_CONFIGURATION_SCHEMA);
    return http_route_manager.getRouteConfigProvider(*rds_config, cm, scope, stat_prefix,
                                                     init_manager);
  }
}

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(const Json::Object& config,
                                                             Runtime::Loader& runtime,
                                                             Upstream::ClusterManager& cm)
    : config_(new ConfigImpl(config, runtime, cm, true)) {}

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
    const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm,
    Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, const std::string& stat_prefix,
    ThreadLocal::SlotAllocator& tls, HttpRouteManagerImpl& http_route_manager)

    : RestApiFetcher(cm, config.getString("cluster"), dispatcher, random,
                     std::chrono::milliseconds(config.getInteger("refresh_delay_ms", 30000))),
      runtime_(runtime), local_info_(local_info), tls_(tls.allocateSlot()),
      route_config_name_(config.getString("route_config_name")),
      stats_({ALL_RDS_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "rds."))}),
      http_route_manager_(http_route_manager) {

  ::Envoy::Config::Utility::checkClusterAndLocalInfo("rds", remote_cluster_name_, cm, local_info);
  ConfigConstSharedPtr initial_config(new NullConfigImpl());
  tls_->set([initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalConfig>(initial_config);
  });
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  onFetchComplete();

  // The ownership of RdsRouteConfigProviderImpl is shared among all HttpConnectionManagers that
  // hold a shared_ptr to it. The HttpRouteManager holds weak_ptrs to the RdsRouteConfigProviders.
  // Therefore, the map entry for the RdsRouteConfigProvider has to get cleaned by the
  // RdsRouteConfigProvider's destructor.
  http_route_manager_.route_config_providers_.erase(route_config_name_ + "_" +
                                                    remote_cluster_name_);
}

Router::ConfigConstSharedPtr RdsRouteConfigProviderImpl::config() {
  return tls_->getTyped<ThreadLocalConfig>().config_;
}

void RdsRouteConfigProviderImpl::createRequest(Http::Message& request) {
  ENVOY_LOG(debug, "rds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(fmt::format("/v1/routes/{}/{}/{}", route_config_name_,
                                                   local_info_.clusterName(),
                                                   local_info_.nodeName()));
}

void RdsRouteConfigProviderImpl::parseResponse(const Http::Message& response) {
  ENVOY_LOG(debug, "rds: parsing response");
  Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response.bodyAsString());
  uint64_t new_hash = response_json->hash();
  if (new_hash != last_config_hash_ || !initialized_) {
    response_json->validateSchema(Json::Schema::ROUTE_CONFIGURATION_SCHEMA);
    ConfigConstSharedPtr new_config(new ConfigImpl(*response_json, runtime_, cm_, false));
    initialized_ = true;
    last_config_hash_ = new_hash;
    stats_.config_reload_.inc();
    ENVOY_LOG(debug, "rds: loading new configuration: config_name={} hash={}", route_config_name_,
              new_hash);
    tls_->runOnAllThreads(
        [this, new_config]() -> void { tls_->getTyped<ThreadLocalConfig>().config_ = new_config; });
  }

  stats_.update_success_.inc();
}

void RdsRouteConfigProviderImpl::onFetchComplete() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

void RdsRouteConfigProviderImpl::onFetchFailure(const EnvoyException* e) {
  stats_.update_failure_.inc();
  if (e) {
    ENVOY_LOG(warn, "rds: fetch failure: {}", e->what());
  } else {
    ENVOY_LOG(info, "rds: fetch failure: network error");
  }
}

void RdsRouteConfigProviderImpl::registerInitTarget(Init::Manager& init_manager) {
  init_manager.registerTarget(*this);
}

HttpRouteManagerImpl::HttpRouteManagerImpl(Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
                                           Runtime::RandomGenerator& random,
                                           const LocalInfo::LocalInfo& local_info,
                                           ThreadLocal::SlotAllocator& tls)
    : runtime_(runtime), dispatcher_(dispatcher), random_(random), local_info_(local_info),
      tls_(tls) {}

std::vector<Router::RouteConfigProviderSharedPtr> HttpRouteManagerImpl::routeConfigProviders() {
  std::vector<Router::RouteConfigProviderSharedPtr> ret;
  ret.reserve(route_config_providers_.size());
  for (const auto& element : route_config_providers_) {
    ret.push_back(element.second.lock());
  }
  return ret;
};

Router::RouteConfigProviderSharedPtr HttpRouteManagerImpl::getRouteConfigProvider(
    const Json::Object& config, Upstream::ClusterManager& cm, Stats::Scope& scope,
    const std::string& stat_prefix, Init::Manager& init_manager) {

  // RdsRouteConfigProviders are unique based on their <route_config_name>_<cluster>.
  std::string map_identifier = config.getString("route_config_name") + "_";
  map_identifier += config.getString("cluster");

  auto it = route_config_providers_.find(map_identifier);
  if (it == route_config_providers_.end()) {
    std::shared_ptr<RdsRouteConfigProviderImpl> new_provider{new RdsRouteConfigProviderImpl(
        config, runtime_, cm, dispatcher_, random_, local_info_, scope, stat_prefix, tls_, *this)};

    new_provider->registerInitTarget(init_manager);

    route_config_providers_.insert(
        {map_identifier, std::weak_ptr<RdsRouteConfigProviderImpl>(new_provider)});

    return std::move(new_provider);
  }

  return it->second.lock();
};

} // namespace Router
} // namespace Envoy
