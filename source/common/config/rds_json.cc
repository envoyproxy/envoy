#include "common/config/rds_json.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/json_utility.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Config {

void RdsJson::translateWeightedCluster(const Json::Object& json_weighted_clusters,
                                       envoy::api::v2::WeightedCluster& weighted_cluster) {
  weighted_cluster.set_runtime_key_prefix(
      json_weighted_clusters.getString("runtime_key_prefix", ""));
  const auto clusters = json_weighted_clusters.getObjectArray("clusters");
  std::transform(clusters.cbegin(), clusters.cend(),
                 Protobuf::RepeatedPtrFieldBackInserter(weighted_cluster.mutable_clusters()),
                 [](const Json::ObjectSharedPtr& json_cluster_weight) {
                   envoy::api::v2::WeightedCluster::ClusterWeight cluster_weight;
                   JSON_UTIL_SET_STRING(*json_cluster_weight, cluster_weight, name);
                   JSON_UTIL_SET_INTEGER(*json_cluster_weight, cluster_weight, weight);
                   return cluster_weight;
                 });
}

void RdsJson::translateVirtualCluster(const Json::Object& json_virtual_cluster,
                                      envoy::api::v2::VirtualCluster& virtual_cluster) {
  JSON_UTIL_SET_STRING(json_virtual_cluster, virtual_cluster, name);
  JSON_UTIL_SET_STRING(json_virtual_cluster, virtual_cluster, pattern);

  envoy::api::v2::RequestMethod method;
  RequestMethod_Parse(json_virtual_cluster.getString("method", "METHOD_UNSPECIFIED"), &method);
  virtual_cluster.set_method(method);
}

void RdsJson::translateRateLimit(const Json::Object& json_rate_limit,
                                 envoy::api::v2::RateLimit& rate_limit) {
  json_rate_limit.validateSchema(Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA);
  JSON_UTIL_SET_INTEGER(json_rate_limit, rate_limit, stage);
  JSON_UTIL_SET_STRING(json_rate_limit, rate_limit, disable_key);
  const auto actions = json_rate_limit.getObjectArray("actions");
  for (const auto json_action : actions) {
    auto* action = rate_limit.mutable_actions()->Add();
    const std::string& type = json_action->getString("type");
    if (type == "source_cluster") {
      action->mutable_source_cluster();
    } else if (type == "destination_cluster") {
      action->mutable_destination_cluster();
    } else if (type == "request_headers") {
      auto* request_headers = action->mutable_request_headers();
      JSON_UTIL_SET_STRING(*json_action, *request_headers, header_name);
      JSON_UTIL_SET_STRING(*json_action, *request_headers, descriptor_key);
    } else if (type == "remote_address") {
      action->mutable_remote_address();
    } else if (type == "generic_key") {
      auto* generic_key = action->mutable_generic_key();
      JSON_UTIL_SET_STRING(*json_action, *generic_key, descriptor_value);
    } else {
      ASSERT(type == "header_value_match");
      auto* header_value_match = action->mutable_header_value_match();
      JSON_UTIL_SET_STRING(*json_action, *header_value_match, descriptor_value);
      JSON_UTIL_SET_BOOL(*json_action, *header_value_match, expect_match);
      const auto headers = json_action->getObjectArray("headers");
      std::transform(headers.cbegin(), headers.cend(),
                     Protobuf::RepeatedPtrFieldBackInserter(header_value_match->mutable_headers()),
                     [](const Json::ObjectSharedPtr& json_header_matcher) {
                       envoy::api::v2::HeaderMatcher header_matcher;
                       translateHeaderMatcher(*json_header_matcher, header_matcher);
                       return header_matcher;
                     });
    }
  }
}

void RdsJson::translateHeaderMatcher(const Json::Object& json_header_matcher,
                                     envoy::api::v2::HeaderMatcher& header_matcher) {
  json_header_matcher.validateSchema(Json::Schema::HEADER_DATA_CONFIGURATION_SCHEMA);
  JSON_UTIL_SET_STRING(json_header_matcher, header_matcher, name);
  JSON_UTIL_SET_STRING(json_header_matcher, header_matcher, value);
  JSON_UTIL_SET_BOOL(json_header_matcher, header_matcher, regex);
}

} // namespace Config
} // namespace Envoy
