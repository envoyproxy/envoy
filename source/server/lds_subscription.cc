#include "server/lds_subscription.h"

#include "envoy/api/v2/listener/listener.pb.h"

#include "common/common/fmt.h"
#include "common/config/lds_json.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Server {

LdsSubscription::LdsSubscription(Config::SubscriptionStats stats,
                                 const envoy::api::v2::core::ConfigSource& lds_config,
                                 Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info)
    : RestApiFetcher(cm, lds_config.api_config_source().cluster_names()[0], dispatcher, random,
                     Config::Utility::apiConfigSourceRefreshDelay(lds_config.api_config_source())),
      local_info_(local_info), stats_(stats) {
  const auto& api_config_source = lds_config.api_config_source();
  UNREFERENCED_PARAMETER(lds_config);
  // If we are building an LdsSubscription, the ConfigSource should be REST_LEGACY.
  ASSERT(api_config_source.api_type() == envoy::api::v2::core::ApiConfigSource::REST_LEGACY);
  // TODO(htuch): Add support for multiple clusters, #1170.
  ASSERT(api_config_source.cluster_names().size() == 1);
  ASSERT(api_config_source.has_refresh_delay());
  Envoy::Config::Utility::checkClusterAndLocalInfo("lds", api_config_source.cluster_names()[0], cm,
                                                   local_info);
}

void LdsSubscription::createRequest(Http::Message& request) {
  ENVOY_LOG(debug, "lds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(
      fmt::format("/v1/listeners/{}/{}", local_info_.clusterName(), local_info_.nodeName()));
}

void LdsSubscription::parseResponse(const Http::Message& response) {
  ENVOY_LOG(debug, "lds: parsing response");
  const std::string response_body = response.bodyAsString();
  Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response_body);
  response_json->validateSchema(Json::Schema::LDS_SCHEMA);
  std::vector<Json::ObjectSharedPtr> json_listeners = response_json->getObjectArray("listeners");

  Protobuf::RepeatedPtrField<envoy::api::v2::Listener> resources;
  for (const Json::ObjectSharedPtr& json_listener : json_listeners) {
    Config::LdsJson::translateListener(*json_listener, *resources.Add());
  }

  std::pair<std::string, uint64_t> hash =
      Envoy::Config::Utility::computeHashedVersion(response_body);
  callbacks_->onConfigUpdate(resources, hash.first);
  stats_.version_.set(hash.second);
  stats_.update_success_.inc();
}

void LdsSubscription::onFetchComplete() {}

void LdsSubscription::onFetchFailure(const EnvoyException* e) {
  callbacks_->onConfigUpdateFailed(e);
  stats_.update_failure_.inc();
  if (e) {
    ENVOY_LOG(warn, "lds: fetch failure: {}", e->what());
  } else {
    ENVOY_LOG(info, "lds: fetch failure: network error");
  }
}

} // namespace Server
} // namespace Envoy
