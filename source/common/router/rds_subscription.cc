#include "common/router/rds_subscription.h"

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Router {

RdsSubscription::RdsSubscription(
    Envoy::Config::SubscriptionStats stats,
    const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, const Stats::Scope& scope)
    : RestApiFetcher(cm, rds.config_source().api_config_source(), dispatcher, random),
      local_info_(local_info), stats_(stats), scope_(scope) {
  Envoy::Config::Utility::checkClusterAndLocalInfo(
      "rds", rds.config_source().api_config_source().cluster_names()[0], cm, local_info);
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
  Envoy::Config::RdsJson::translateRouteConfiguration(*response_json, *resources.Add(),
                                                      scope_.statsOptions());
  resources[0].set_name(route_config_name_);
  std::pair<std::string, uint64_t> hash =
      Envoy::Config::Utility::computeHashedVersion(response_body);
  callbacks_->onConfigUpdate(resources, hash.first);
  stats_.version_.set(hash.second);
  stats_.update_success_.inc();
}

void RdsSubscription::onFetchComplete() {}

void RdsSubscription::onFetchFailure(const EnvoyException* e) {
  callbacks_->onConfigUpdateFailed(e);
  if (e) {
    stats_.update_rejected_.inc();
    ENVOY_LOG(warn, "rds: fetch failure: {}", e->what());
  } else {
    stats_.update_failure_.inc();
    ENVOY_LOG(debug, "rds: fetch failure: network error");
  }
}

} // namespace Router
} // namespace Envoy
