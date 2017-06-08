#include "common/upstream/cds_api_impl.h"

#include <chrono>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiImpl::create(const Json::Object& config, ClusterManager& cm,
                             Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope) {
  if (!config.hasObject("cds")) {
    return nullptr;
  }

  return CdsApiPtr{
      new CdsApiImpl(*config.getObject("cds"), cm, dispatcher, random, local_info, scope)};
}

CdsApiImpl::CdsApiImpl(const Json::Object& config, ClusterManager& cm,
                       Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
    : RestApiFetcher(cm, config.getObject("cluster")->getString("name"), dispatcher, random,
                     std::chrono::milliseconds(config.getInteger("refresh_delay_ms", 30000))),
      local_info_(local_info),
      stats_({ALL_CDS_STATS(POOL_COUNTER_PREFIX(scope, "cluster_manager.cds."))}) {
  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    throw EnvoyException("cds: setting --service-cluster and --service-node are required");
  }
}

void CdsApiImpl::createRequest(Http::Message& request) {
  log_facility(debug, "cds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(
      fmt::format("/v1/clusters/{}/{}", local_info_.clusterName(), local_info_.nodeName()));
}

void CdsApiImpl::parseResponse(const Http::Message& response) {
  log_facility(debug, "cds: parsing response");
  Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response.bodyAsString());
  response_json->validateSchema(Json::Schema::CDS_SCHEMA);
  std::vector<Json::ObjectSharedPtr> clusters = response_json->getObjectArray("clusters");

  // We need to keep track of which clusters we might need to remove.
  ClusterManager::ClusterInfoMap clusters_to_remove = cm_.clusters();
  for (auto& cluster : clusters) {
    std::string cluster_name = cluster->getString("name");
    clusters_to_remove.erase(cluster_name);
    if (cm_.addOrUpdatePrimaryCluster(*cluster)) {
      log_facility(info, "cds: add/update cluster '{}'", cluster_name);
    }
  }

  for (auto cluster : clusters_to_remove) {
    if (cm_.removePrimaryCluster(cluster.first)) {
      log_facility(info, "cds: remove cluster '{}'", cluster.first);
    }
  }

  stats_.update_success_.inc();
}

void CdsApiImpl::onFetchComplete() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

void CdsApiImpl::onFetchFailure(EnvoyException* e) {
  stats_.update_failure_.inc();
  if (e) {
    log_facility(warn, "cds: fetch failure: {}", e->what());
  } else {
    log_facility(info, "cds: fetch failure: network error");
  }
}

} // Upstream
} // Envoy
