#pragma once

#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/stats/stats_options.h"

namespace Envoy {
namespace Config {

class RdsJson {
public:
  /**
   * Translate a v1 JSON weighted clusters object to v2 envoy::api::v2::route::WeightedCluster.
   * @param json_weighted_clusters source v1 JSON weighted clusters object.
   * @param weighted_cluster destination v2 envoy::api::v2::route::WeightedCluster.
   */
  static void translateWeightedCluster(const Json::Object& json_weighted_clusters,
                                       envoy::api::v2::route::WeightedCluster& weighted_cluster);

  /**
   * Translate a v1 JSON virtual cluster object to v2 envoy::api::v2::route::VirtualCluster.
   * @param json_virtual_cluster source v1 JSON virtual cluster object.
   * @param virtual_cluster destination v2 envoy::api::v2::route::VirtualCluster.
   */
  static void translateVirtualCluster(const Json::Object& json_virtual_cluster,
                                      envoy::api::v2::route::VirtualCluster& virtual_cluster);

  /**
   * Translate a v1 JSON cors object to v2 envoy::api::v2::route::CorsPolicy.
   * @param json_cors source v1 JSON cors object.
   * @param cors destination v2 envoy::api::v2::route::CorsPolicy.
   */
  static void translateCors(const Json::Object& json_cors, envoy::api::v2::route::CorsPolicy& cors);

  /**
   * Translate a v1 JSON rate limit object to v2 envoy::api::v2::route::RateLimit.
   * @param json_rate_limit source v1 JSON rate limit object.
   * @param rate_limit destination v2 envoy::api::v2::route::RateLimit.
   */
  static void translateRateLimit(const Json::Object& json_rate_limit,
                                 envoy::api::v2::route::RateLimit& rate_limit);

  /**
   * Translate a v1 JSON header matcher object to v2 envoy::api::v2::route::HeaderMatcher.
   * @param json_header_matcher source v1 JSON header matcher object.
   * @param header_matcher destination v2 envoy::api::v2::route::HeaderMatcher.
   */
  static void translateHeaderMatcher(const Json::Object& json_header_matcher,
                                     envoy::api::v2::route::HeaderMatcher& header_matcher);

  /**
   * Translate a v1 JSON query parameter matcher object to v2
   * envoy::api::v2::route::QueryParameterMatcher.
   * @param json_query_parameter_matcher source v1 JSON query parameter matcher object.
   * @param query_parameter_matcher destination v2 envoy::api::v2::route::QueryParameterMatcher.
   */
  static void translateQueryParameterMatcher(
      const Json::Object& json_query_parameter_matcher,
      envoy::api::v2::route::QueryParameterMatcher& query_parameter_matcher);

  /**
   * Translate a v1 JSON route configuration object to v2 envoy::api::v2::RouteConfiguration.
   * @param json_route_config source v1 JSON route configuration object.
   * @param route_config destination v2 envoy::api::v2::RouteConfiguration.
   */
  static void translateRouteConfiguration(const Json::Object& json_route_config,
                                          envoy::api::v2::RouteConfiguration& route_config,
                                          const Stats::StatsOptions& stats_options);

  /**
   * Translate a v1 JSON virtual host object to v2 envoy::api::v2::route::VirtualHost.
   * @param json_virtual_host source v1 JSON virtual host object.
   * @param virtual_host destination v2 envoy::api::v2::route::VirtualHost.
   */
  static void translateVirtualHost(const Json::Object& json_virtual_host,
                                   envoy::api::v2::route::VirtualHost& virtual_host,
                                   const Stats::StatsOptions& stats_options);

  /**
   * Translate a v1 JSON decorator object to v2 envoy::api::v2::route::Decorator.
   * @param json_decorator source v1 JSON decorator object.
   * @param decorator destination v2 envoy::api::v2::route::Decorator.
   */
  static void translateDecorator(const Json::Object& json_decorator,
                                 envoy::api::v2::route::Decorator& decorator);

  /**
   * Translate a v1 JSON route object to v2 envoy::api::v2::route::Route.
   * @param json_route source v1 JSON route object.
   * @param route destination v2 envoy::api::v2::route::Route.
   */
  static void translateRoute(const Json::Object& json_route, envoy::api::v2::route::Route& route);
};

} // namespace Config
} // namespace Envoy
