#include "common/upstream/sds_subscription.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

#include "api/eds.pb.h"
#include "google/protobuf/util/time_util.h"

namespace Envoy {
namespace Upstream {

SdsSubscription::SdsSubscription(ClusterStats& stats,
                                 const envoy::api::v2::ConfigSource& eds_config, ClusterManager& cm,
                                 Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random)
    : RestApiFetcher(cm, eds_config.api_config_source().cluster_name()[0], dispatcher, random,
                     Config::Utility::apiConfigSourceRefreshDelay(eds_config.api_config_source())),
      stats_(stats) {
  const auto& api_config_source = eds_config.api_config_source();
  UNREFERENCED_PARAMETER(api_config_source);
  // If we are building an SdsSubscription, the ConfigSource should be REST_LEGACY.
  ASSERT(api_config_source.api_type() == envoy::api::v2::ApiConfigSource::REST_LEGACY);
  // TODO(htuch): Add support for multiple clusters, #1170.
  ASSERT(api_config_source.cluster_name().size() == 1);
  ASSERT(api_config_source.has_refresh_delay());
}

void SdsSubscription::parseResponse(const Http::Message& response) {
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response.bodyAsString());
  json->validateSchema(Json::Schema::SDS_SCHEMA);

  // Since in the v2 EDS API we place all the endpoints for a given zone in the same proto, we first
  // need to bin the returned hosts list so that we group them by zone. We use an ordered map here
  // to provide better determinism for debug/test behavior.
  std::map<std::string, google::protobuf::RepeatedPtrField<envoy::api::v2::LbEndpoint>>
      zone_lb_endpoints;
  for (const Json::ObjectSharedPtr& host : json->getObjectArray("hosts")) {
    bool canary = false;
    uint32_t weight = 1;
    std::string zone = "";
    if (host->hasObject("tags")) {
      canary = host->getObject("tags")->getBoolean("canary", canary);
      weight = host->getObject("tags")->getInteger("load_balancing_weight", weight);
      zone = host->getObject("tags")->getString("az", zone);
    }
    auto* lb_endpoint = zone_lb_endpoints[zone].Add();
    auto* address = lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
    address->set_ip_address(host->getString("ip_address"));
    address->mutable_port()->set_value(host->getInteger("port"));
    // TODO(htuch): This will eventually be generalized metadata/labels, see
    // https://github.com/lyft/envoy-api/issues/81.
    lb_endpoint->mutable_canary()->set_value(canary);
    lb_endpoint->mutable_load_balancing_weight()->set_value(weight);
  }

  google::protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name(cluster_name_);
  for (auto it : zone_lb_endpoints) {
    auto* locality_lb_endpoints = cluster_load_assignment->add_endpoints();
    locality_lb_endpoints->mutable_locality()->set_zone(it.first);
    locality_lb_endpoints->mutable_lb_endpoints()->Swap(&it.second);
  }

  callbacks_->onConfigUpdate(resources);
  stats_.update_success_.inc();
}

void SdsSubscription::onFetchFailure(const EnvoyException* e) {
  callbacks_->onConfigUpdateFailed(e);
  ENVOY_LOG(debug, "sds refresh failure for cluster: {}", cluster_name_);
  stats_.update_failure_.inc();
  if (e) {
    ENVOY_LOG(warn, "sds parsing error: {}", e->what());
  }
}

void SdsSubscription::createRequest(Http::Message& message) {
  ENVOY_LOG(debug, "starting sds refresh for cluster: {}", cluster_name_);
  stats_.update_attempt_.inc();

  message.headers().insertMethod().value(Http::Headers::get().MethodValues.Get);
  message.headers().insertPath().value("/v1/registration/" + cluster_name_);
}

void SdsSubscription::onFetchComplete() {
  ENVOY_LOG(debug, "sds refresh complete for cluster: {}", cluster_name_);
}

} // namespace Upstream
} // namespace Envoy
