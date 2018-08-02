#include "common/upstream/sds_subscription.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/common/exception.h"

#include "common/config/metadata.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

SdsSubscription::SdsSubscription(ClusterStats& stats,
                                 const envoy::api::v2::core::ConfigSource& eds_config,
                                 ClusterManager& cm, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random)
    : RestApiFetcher(cm, eds_config.api_config_source(), dispatcher, random), stats_(stats) {}

void SdsSubscription::parseResponse(const Http::Message& response) {
  const std::string response_body = response.bodyAsString();
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response_body);
  json->validateSchema(Json::Schema::SDS_SCHEMA);

  // Since in the v2 EDS API we place all the endpoints for a given zone in the same proto, we first
  // need to bin the returned hosts list so that we group them by zone. We use an ordered map here
  // to provide better determinism for debug/test behavior.
  std::map<std::string, Protobuf::RepeatedPtrField<envoy::api::v2::endpoint::LbEndpoint>>
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
    address->set_address(host->getString("ip_address"));
    address->set_port_value(host->getInteger("port"));
    Config::Metadata::mutableMetadataValue(*lb_endpoint->mutable_metadata(),
                                           Config::MetadataFilters::get().ENVOY_LB,
                                           Config::MetadataEnvoyLbKeys::get().CANARY)
        .set_bool_value(canary);
    lb_endpoint->mutable_load_balancing_weight()->set_value(weight);
  }

  Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> resources;
  auto* cluster_load_assignment = resources.Add();
  cluster_load_assignment->set_cluster_name(cluster_name_);
  for (auto it : zone_lb_endpoints) {
    auto* locality_lb_endpoints = cluster_load_assignment->add_endpoints();
    locality_lb_endpoints->mutable_locality()->set_zone(it.first);
    locality_lb_endpoints->mutable_lb_endpoints()->Swap(&it.second);
  }

  std::pair<std::string, uint64_t> hash =
      Envoy::Config::Utility::computeHashedVersion(response_body);
  callbacks_->onConfigUpdate(resources, hash.first);
  stats_.version_.set(hash.second);
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

  message.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  message.headers().insertPath().value("/v1/registration/" + cluster_name_);
}

void SdsSubscription::onFetchComplete() {
  ENVOY_LOG(debug, "sds refresh complete for cluster: {}", cluster_name_);
}

} // namespace Upstream
} // namespace Envoy
