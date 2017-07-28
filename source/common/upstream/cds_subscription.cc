#include "common/upstream/cds_subscription.h"

#include <vector>

#include "common/config/cds_json.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Upstream {

CdsSubscription::CdsSubscription(Config::SubscriptionStats stats,
                                 const envoy::api::v2::ConfigSource& cds_config,
                                 const Optional<SdsConfig>& sds_config, ClusterManager& cm,
                                 Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info)
    : RestApiFetcher(cm, cds_config.api_config_source().cluster_name()[0], dispatcher, random,
                     Config::Utility::apiConfigSourceRefreshDelay(cds_config.api_config_source())),
      local_info_(local_info), stats_(stats), sds_config_(sds_config) {
  const auto& api_config_source = cds_config.api_config_source();
  UNREFERENCED_PARAMETER(api_config_source);
  // If we are building an CdsSubscription, the ConfigSource should be REST_LEGACY.
  ASSERT(api_config_source.api_type() == envoy::api::v2::ApiConfigSource::REST_LEGACY);
  // TODO(htuch): Add support for multiple clusters, #1170.
  ASSERT(api_config_source.cluster_name().size() == 1);
  ASSERT(api_config_source.has_refresh_delay());
}

void CdsSubscription::createRequest(Http::Message& request) {
  ENVOY_LOG(debug, "cds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(
      fmt::format("/v1/clusters/{}/{}", local_info_.clusterName(), local_info_.nodeName()));
}

void CdsSubscription::parseResponse(const Http::Message& response) {
  ENVOY_LOG(debug, "cds: parsing response");
  Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response.bodyAsString());
  response_json->validateSchema(Json::Schema::CDS_SCHEMA);
  std::vector<Json::ObjectSharedPtr> clusters = response_json->getObjectArray("clusters");

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> resources;
  for (const Json::ObjectSharedPtr& cluster : clusters) {
    Config::CdsJson::translateCluster(*cluster, sds_config_, *resources.Add());
  }

  callbacks_->onConfigUpdate(resources);
  stats_.update_success_.inc();
}

void CdsSubscription::onFetchComplete() {}

void CdsSubscription::onFetchFailure(const EnvoyException* e) {
  callbacks_->onConfigUpdateFailed(e);
  stats_.update_failure_.inc();
  if (e) {
    ENVOY_LOG(warn, "cds: fetch failure: {}", e->what());
  } else {
    ENVOY_LOG(info, "cds: fetch failure: network error");
  }
}

} // namespace Upstream
} // namespace Envoy
