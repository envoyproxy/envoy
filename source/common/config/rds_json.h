#pragma once

#include "envoy/json/json_object.h"

#include "api/rds.pb.h"

namespace Envoy {
namespace Config {

class RdsJson {
public:
  /**
   * Translate a v1 JSON weighted clusters object to v2 envoy::api::v2::WeightedCluster.
   * @param json_weighted_clusters source v1 JSON weighted clusters object.
   * @param weighted_cluster destination v2 envoy::api::v2::WeightedCluster.
   */
  static void translateWeightedCluster(const Json::Object& json_weighted_clusters,
                                       envoy::api::v2::WeightedCluster& weighted_cluster);

  /**
   * Translate a v1 JSON virtual cluster object to v2 envoy::api::v2::VirtualCluster.
   * @param json_virtual_cluster source v1 JSON virtual cluster object.
   * @param virtual_cluster destination v2 envoy::api::v2::VirtualCluster.
   */
  static void translateVirtualCluster(const Json::Object& json_virtual_cluster,
                                      envoy::api::v2::VirtualCluster& virtual_cluster);

  /**
   * Translate a v1 JSON rate limit object to v2 envoy::api::v2::RateLimit.
   * @param json_rate_limit source v1 JSON rate limit object.
   * @param rate_limit destination v2 envoy::api::v2::RateLimit.
   */
  static void translateRateLimit(const Json::Object& json_rate_limit,
                                 envoy::api::v2::RateLimit& rate_limit);

  /**
   * Translate a v1 JSON header matcher object to v2 envoy::api::v2::HeaderMatcher.
   * @param json_header_matcher source v1 JSON header matcher object.
   * @param header_matcher destination v2 envoy::api::v2::HeaderMatcher.
   */
  static void translateHeaderMatcher(const Json::Object& json_header_matcher,
                                     envoy::api::v2::HeaderMatcher& header_matcher);
};

} // namespace Config
} // namespace Envoy
