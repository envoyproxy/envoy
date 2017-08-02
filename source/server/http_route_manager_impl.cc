#include "server/http_route_manager_impl.h"

#include <string>

#include "common/config/utility.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Server {

RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl(
  const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm,
  Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
  const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, const std::string& stat_prefix,
  ThreadLocal::SlotAllocator& tls)

  : RestApiFetcher(cm, config.getString("cluster"), dispatcher, random,
                   std::chrono::milliseconds(config.getInteger("refresh_delay_ms", 30000))),
    runtime_(runtime), local_info_(local_info), tls_(tls.allocateSlot()),
    route_config_name_(config.getString("route_config_name")),
    stats_({ALL_RDS_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "rds."))}) {

  ::Envoy::Config::Utility::checkClusterAndLocalInfo("rds", remote_cluster_name_, cm, local_info);
  Router::ConfigConstSharedPtr initial_config(new Router::NullConfigImpl());
  tls_->set([initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalConfig>(initial_config);
  });
}

RdsRouteConfigProviderImpl::~RdsRouteConfigProviderImpl() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  onFetchComplete();
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
    Router::ConfigConstSharedPtr new_config(new Router::ConfigImpl(*response_json, runtime_, cm_, false));
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

} // namespace Router
} // namespace Envoy