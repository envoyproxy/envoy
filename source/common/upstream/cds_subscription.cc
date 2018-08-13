#include "common/upstream/cds_subscription.h"

#include <vector>

#include "common/common/fmt.h"
#include "common/config/cds_json.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Upstream {

CdsSubscription::CdsSubscription(
    Config::SubscriptionStats stats, const envoy::api::v2::core::ConfigSource& cds_config,
    const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config, ClusterManager& cm,
    Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, const Stats::StatsOptions& stats_options)
    : RestApiFetcher(cm, cds_config.api_config_source(), dispatcher, random),
      local_info_(local_info), stats_(stats), eds_config_(eds_config),
      stats_options_(stats_options) {}

void CdsSubscription::createRequest(Http::Message& request) {
  ENVOY_LOG(debug, "cds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(
      fmt::format("/v1/clusters/{}/{}", local_info_.clusterName(), local_info_.nodeName()));
  request.headers().insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);
  request.headers().insertContentLength().value(size_t(0));
}

void CdsSubscription::parseResponse(const Http::Message& response) {
  ENVOY_LOG(debug, "cds: parsing response");
  const std::string response_body = response.bodyAsString();
  Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response_body);
  response_json->validateSchema(Json::Schema::CDS_SCHEMA);
  std::vector<Json::ObjectSharedPtr> clusters = response_json->getObjectArray("clusters");

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> resources;
  for (const Json::ObjectSharedPtr& cluster : clusters) {
    Config::CdsJson::translateCluster(*cluster, eds_config_, *resources.Add(), stats_options_);
  }

  std::pair<std::string, uint64_t> hash =
      Envoy::Config::Utility::computeHashedVersion(response_body);
  callbacks_->onConfigUpdate(resources, hash.first);
  stats_.version_.set(hash.second);
  stats_.update_success_.inc();
}

void CdsSubscription::onFetchComplete() {}

void CdsSubscription::onFetchFailure(const EnvoyException* e) {
  callbacks_->onConfigUpdateFailed(e);
  stats_.update_failure_.inc();
  if (e) {
    ENVOY_LOG(warn, "cds: fetch failure: {}", e->what());
  } else {
    ENVOY_LOG(debug, "cds: fetch failure: network error");
  }
}

} // namespace Upstream
} // namespace Envoy
