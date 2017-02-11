#include "config_impl.h"
#include "rds_impl.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"

namespace Router {

RouteConfigProviderPtr
RouteConfigProviderUtil::create(const Json::Object& config, Runtime::Loader& runtime,
                                Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                                Runtime::RandomGenerator& random,
                                const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                                const std::string& stat_prefix, ThreadLocal::Instance& tls) {
  bool has_rds = config.hasObject("rds");
  bool has_route_config = config.hasObject("route_config");
  if (!(has_rds ^ has_route_config)) {
    throw EnvoyException(
        "http connection manager must have either rds or route_config but not both");
  }

  if (has_route_config) {
    return RouteConfigProviderPtr{
        new StaticRouteConfigProviderImpl(*config.getObject("route_config"), runtime, cm)};
  } else {
    // TODO: Ordered initialization of RDS: 1) CDS/clusters, 2) RDS, 3) start listening. This
    //       will be done in a follow up where we will add a formal init handler in the server.
    Json::ObjectPtr rds_config = config.getObject("rds");
    rds_config->validateSchema(Json::Schema::RDS_CONFIGURATION_SCHEMA);
    std::unique_ptr<RdsRouteConfigProviderImpl> provider{new RdsRouteConfigProviderImpl(
        *rds_config, runtime, cm, dispatcher, random, local_info, scope, stat_prefix, tls)};
    provider->initialize();
    return std::move(provider);
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
    ThreadLocal::Instance& tls)

    : RestApiFetcher(cm, config.getString("cluster"), dispatcher, random,
                     std::chrono::milliseconds(config.getInteger("refresh_delay_ms", 30000))),
      runtime_(runtime), local_info_(local_info), tls_(tls), tls_slot_(tls.allocateSlot()),
      route_config_name_(config.getString("route_config_name")),
      stats_({ALL_RDS_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix + "rds."))}) {

  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    throw EnvoyException("rds: setting --service-cluster and --service-node are required");
  }

  ConfigPtr initial_config(new NullConfigImpl());
  tls_.set(tls_slot_, [initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectPtr {
    return ThreadLocal::ThreadLocalObjectPtr{new ThreadLocalConfig(initial_config)};
  });
}

Router::ConfigPtr RdsRouteConfigProviderImpl::config() {
  return tls_.getTyped<ThreadLocalConfig>(tls_slot_).config_;
}

void RdsRouteConfigProviderImpl::createRequest(Http::Message& request) {
  log_debug("rds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(fmt::format("/v1/routes/{}/{}/{}", route_config_name_,
                                                   local_info_.clusterName(),
                                                   local_info_.nodeName()));
}

void RdsRouteConfigProviderImpl::parseResponse(const Http::Message& response) {
  log_debug("rds: parsing response");
  Json::ObjectPtr response_json = Json::Factory::LoadFromString(response.bodyAsString());
  uint64_t new_hash = response_json->hash();
  if (new_hash != last_config_hash_ || !initialized_) {
    response_json->validateSchema(Json::Schema::ROUTE_CONFIGURATION_SCHEMA);
    ConfigPtr new_config(new ConfigImpl(*response_json, runtime_, cm_, false));
    initialized_ = true;
    last_config_hash_ = new_hash;
    stats_.config_reload_.inc();
    log_debug("rds: loading new configuration: config_name={} hash={}", route_config_name_,
              new_hash);
    tls_.runOnAllThreads([this, new_config]() -> void {
      tls_.getTyped<ThreadLocalConfig>(tls_slot_).config_ = new_config;
    });
  }

  stats_.update_success_.inc();
}

void RdsRouteConfigProviderImpl::onFetchFailure(EnvoyException* e) {
  stats_.update_failure_.inc();
  if (e) {
    log().warn("rds: fetch failure: {}", e->what());
  } else {
    log().info("rds: fetch failure: network error");
  }
}

} // Router
