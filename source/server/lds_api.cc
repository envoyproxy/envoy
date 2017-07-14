#include "server/lds_api.h"

#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Server {

LdsApi::LdsApi(const Json::Object& config, Upstream::ClusterManager& cm,
               Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
               Init::Manager& init_manager, const LocalInfo::LocalInfo& local_info,
               Stats::Scope& scope, ListenerManager& lm)
    : Json::Validator(config, Json::Schema::LDS_CONFIG_SCHEMA),
      RestApiFetcher(cm, config.getString("cluster"), dispatcher, random,
                     std::chrono::milliseconds(config.getInteger("refresh_delay_ms", 30000))),
      local_info_(local_info), listener_manager_(lm),
      stats_({ALL_LDS_STATS(POOL_COUNTER_PREFIX(scope, "listener_manager.lds."))}) {

  Config::Utility::checkClusterAndLocalInfo("lds", remote_cluster_name_, cm, local_info);
  init_manager.registerTarget(*this);
}

void LdsApi::initialize(std::function<void()> callback) {
  initialize_callback_ = callback;
  RestApiFetcher::initialize();
}

void LdsApi::createRequest(Http::Message& request) {
  ENVOY_LOG(debug, "lds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(
      fmt::format("/v1/listeners/{}/{}", local_info_.clusterName(), local_info_.nodeName()));
}

void LdsApi::parseResponse(const Http::Message& response) {
  ENVOY_LOG(debug, "lds: parsing response");
  Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response.bodyAsString());
  response_json->validateSchema(Json::Schema::LDS_SCHEMA);
  std::vector<Json::ObjectSharedPtr> json_listeners = response_json->getObjectArray("listeners");

  // We need to keep track of which listeners we might need to remove.
  std::unordered_map<std::string, std::reference_wrapper<Listener>> listeners_to_remove;
  for (const auto& listener : listener_manager_.listeners()) {
    listeners_to_remove.emplace(listener.get().name(), listener);
  }

  for (const auto& listener : json_listeners) {
    const std::string listener_name = listener->getString("name");
    listeners_to_remove.erase(listener_name);
    if (listener_manager_.addOrUpdateListener(*listener)) {
      ENVOY_LOG(info, "lds: add/update listener '{}'", listener_name);
    } else {
      ENVOY_LOG(debug, "lds: add/update listener '{}' skipped", listener_name);
    }
  }

  for (const auto& listener : listeners_to_remove) {
    if (listener_manager_.removeListener(listener.first)) {
      ENVOY_LOG(info, "lds: remove listener '{}'", listener.first);
    }
  }

  stats_.update_success_.inc();
}

void LdsApi::onFetchComplete() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

void LdsApi::onFetchFailure(const EnvoyException* e) {
  stats_.update_failure_.inc();
  if (e) {
    ENVOY_LOG(warn, "lds: fetch failure: {}", e->what());
  } else {
    ENVOY_LOG(info, "lds: fetch failure: network error");
  }
}

} // namespace Server
} // namespace Envoy
