#include "common/router/rds_subscription.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Router {

RdsSubscription::RdsSubscription(Envoy::Config::SubscriptionStats stats,
                                 const envoy::api::v2::filter::network::Rds& rds,
                                 Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info)
    : RestApiFetcher(cm, rds.config_source().api_config_source().cluster_names()[0], dispatcher,
                     random,
                     Envoy::Config::Utility::apiConfigSourceRefreshDelay(
                         rds.config_source().api_config_source())),
      local_info_(local_info), stats_(stats) {
  const auto& api_config_source = rds.config_source().api_config_source();
  UNREFERENCED_PARAMETER(api_config_source);
  // If we are building an RdsSubscription, the ConfigSource should be REST_LEGACY.
  ASSERT(api_config_source.api_type() == envoy::api::v2::ApiConfigSource::REST_LEGACY);
  // TODO(htuch): Add support for multiple clusters, #1170.
  ASSERT(api_config_source.cluster_names().size() == 1);
  ASSERT(api_config_source.has_refresh_delay());
  Envoy::Config::Utility::checkClusterAndLocalInfo("rds", api_config_source.cluster_names()[0], cm,
                                                   local_info);
}

void RdsSubscription::createRequest(Http::Message& request) {
  ENVOY_LOG(debug, "rds: starting request");
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  request.headers().insertPath().value(fmt::format("/v1/routes/{}/{}/{}", route_config_name_,
                                                   local_info_.clusterName(),
                                                   local_info_.nodeName()));
}

void RdsSubscription::parseResponse(const Http::Message& response) {
  ENVOY_LOG(debug, "rds: parsing response");
  const std::string response_body = response.bodyAsString();
  Json::ObjectSharedPtr response_json = Json::Factory::loadFromString(response_body);
  Protobuf::RepeatedPtrField<envoy::api::v2::RouteConfiguration> resources;
  Envoy::Config::RdsJson::translateRouteConfiguration(*response_json, *resources.Add());
  resources[0].set_name(route_config_name_);
  callbacks_->onConfigUpdate(resources);
  std::pair<std::string, uint64_t> hash =
      Envoy::Config::Utility::computeHashedVersion(response_body);
  version_info_ = hash.first;
  stats_.version_.set(hash.second);
  stats_.update_success_.inc();
}

void RdsSubscription::onFetchComplete() {}

void RdsSubscription::onFetchFailure(const EnvoyException* e) {
  callbacks_->onConfigUpdateFailed(e);
  stats_.update_failure_.inc();
  if (e) {
    ENVOY_LOG(warn, "rds: fetch failure: {}", e->what());
  } else {
    ENVOY_LOG(debug, "rds: fetch failure: network error");
  }
}

} // namespace Router
} // namespace Envoy
