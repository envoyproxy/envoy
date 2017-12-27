#include "common/config/rds_json.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/base_json.h"
#include "common/config/json_utility.h"
#include "common/config/metadata.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Config {

void RdsJson::translateWeightedCluster(const Json::Object& json_weighted_clusters,
                                       envoy::api::v2::WeightedCluster& weighted_cluster) {
  JSON_UTIL_SET_STRING(json_weighted_clusters, weighted_cluster, runtime_key_prefix);
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

  envoy::api::v2::RequestMethod method{};
  RequestMethod_Parse(json_virtual_cluster.getString("method", "METHOD_UNSPECIFIED"), &method);
  virtual_cluster.set_method(method);
}

void RdsJson::translateCors(const Json::Object& json_cors, envoy::api::v2::CorsPolicy& cors) {
  for (const std::string& origin : json_cors.getStringArray("allow_origin", true)) {
    cors.add_allow_origin(origin);
  }
  JSON_UTIL_SET_STRING(json_cors, cors, allow_methods);
  JSON_UTIL_SET_STRING(json_cors, cors, allow_headers);
  JSON_UTIL_SET_STRING(json_cors, cors, expose_headers);
  JSON_UTIL_SET_STRING(json_cors, cors, max_age);
  JSON_UTIL_SET_BOOL(json_cors, cors, allow_credentials);
  JSON_UTIL_SET_BOOL(json_cors, cors, enabled);
}

void RdsJson::translateRateLimit(const Json::Object& json_rate_limit,
                                 envoy::api::v2::RateLimit& rate_limit) {
  json_rate_limit.validateSchema(Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA);
  JSON_UTIL_SET_INTEGER(json_rate_limit, rate_limit, stage);
  JSON_UTIL_SET_STRING(json_rate_limit, rate_limit, disable_key);
  const auto actions = json_rate_limit.getObjectArray("actions");
  for (const auto json_action : actions) {
    auto* action = rate_limit.mutable_actions()->Add();
    const std::string type = json_action->getString("type");
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

void RdsJson::translateQueryParameterMatcher(
    const Json::Object& json_query_parameter_matcher,
    envoy::api::v2::QueryParameterMatcher& query_parameter_matcher) {
  json_query_parameter_matcher.validateSchema(Json::Schema::QUERY_PARAMETER_CONFIGURATION_SCHEMA);
  JSON_UTIL_SET_STRING(json_query_parameter_matcher, query_parameter_matcher, name);
  JSON_UTIL_SET_STRING(json_query_parameter_matcher, query_parameter_matcher, value);
  JSON_UTIL_SET_BOOL(json_query_parameter_matcher, query_parameter_matcher, regex);
}

void RdsJson::translateRouteConfiguration(const Json::Object& json_route_config,
                                          envoy::api::v2::RouteConfiguration& route_config) {
  json_route_config.validateSchema(Json::Schema::ROUTE_CONFIGURATION_SCHEMA);

  for (const auto json_virtual_host : json_route_config.getObjectArray("virtual_hosts", true)) {
    auto* virtual_host = route_config.mutable_virtual_hosts()->Add();
    translateVirtualHost(*json_virtual_host, *virtual_host);
  }

  for (const std::string& header :
       json_route_config.getStringArray("internal_only_headers", true)) {
    route_config.add_internal_only_headers(header);
  }

  for (const auto header_value :
       json_route_config.getObjectArray("response_headers_to_add", true)) {
    auto* header_value_option = route_config.mutable_response_headers_to_add()->Add();
    BaseJson::translateHeaderValueOption(*header_value, *header_value_option);
  }

  for (const std::string& header :
       json_route_config.getStringArray("response_headers_to_remove", true)) {
    route_config.add_response_headers_to_remove(header);
  }

  for (const auto header_value : json_route_config.getObjectArray("request_headers_to_add", true)) {
    auto* header_value_option = route_config.mutable_request_headers_to_add()->Add();
    BaseJson::translateHeaderValueOption(*header_value, *header_value_option);
  }

  JSON_UTIL_SET_BOOL(json_route_config, route_config, validate_clusters);
}

void RdsJson::translateVirtualHost(const Json::Object& json_virtual_host,
                                   envoy::api::v2::VirtualHost& virtual_host) {
  json_virtual_host.validateSchema(Json::Schema::VIRTUAL_HOST_CONFIGURATION_SCHEMA);

  const std::string name = json_virtual_host.getString("name", "");
  Utility::checkObjNameLength("Invalid virtual host name", name);
  virtual_host.set_name(name);

  for (const std::string& domain : json_virtual_host.getStringArray("domains", true)) {
    virtual_host.add_domains(domain);
  }

  for (const auto json_route : json_virtual_host.getObjectArray("routes", true)) {
    auto* route = virtual_host.mutable_routes()->Add();
    translateRoute(*json_route, *route);
  }

  envoy::api::v2::VirtualHost::TlsRequirementType tls_requirement{};
  envoy::api::v2::VirtualHost::TlsRequirementType_Parse(
      StringUtil::toUpper(json_virtual_host.getString("require_ssl", "")), &tls_requirement);
  virtual_host.set_require_tls(tls_requirement);

  for (const auto json_virtual_cluster :
       json_virtual_host.getObjectArray("virtual_clusters", true)) {
    auto* virtual_cluster = virtual_host.mutable_virtual_clusters()->Add();
    translateVirtualCluster(*json_virtual_cluster, *virtual_cluster);
  }

  for (const auto json_rate_limit : json_virtual_host.getObjectArray("rate_limits", true)) {
    auto* rate_limit = virtual_host.mutable_rate_limits()->Add();
    translateRateLimit(*json_rate_limit, *rate_limit);
  }

  for (const auto header_value : json_virtual_host.getObjectArray("request_headers_to_add", true)) {
    auto* header_value_option = virtual_host.mutable_request_headers_to_add()->Add();
    BaseJson::translateHeaderValueOption(*header_value, *header_value_option);
  }

  if (json_virtual_host.hasObject("cors")) {
    auto* cors = virtual_host.mutable_cors();
    const auto json_cors = json_virtual_host.getObject("cors");
    translateCors(*json_cors, *cors);
  }
}

void RdsJson::translateDecorator(const Json::Object& json_decorator,
                                 envoy::api::v2::Decorator& decorator) {
  if (json_decorator.hasObject("operation")) {
    decorator.set_operation(json_decorator.getString("operation"));
  }
}

void RdsJson::translateRoute(const Json::Object& json_route, envoy::api::v2::Route& route) {
  json_route.validateSchema(Json::Schema::ROUTE_ENTRY_CONFIGURATION_SCHEMA);

  auto* match = route.mutable_match();

  // This is a trick to do a three-way XOR.
  if ((json_route.hasObject("prefix") + json_route.hasObject("path") +
       json_route.hasObject("regex")) != 1) {
    throw EnvoyException("routes must specify one of prefix/path/regex");
  }

  if (json_route.hasObject("prefix")) {
    match->set_prefix(json_route.getString("prefix"));
  } else if (json_route.hasObject("path")) {
    match->set_path(json_route.getString("path"));
  } else {
    ASSERT(json_route.hasObject("regex"));
    match->set_regex(json_route.getString("regex"));
  }

  JSON_UTIL_SET_BOOL(json_route, *match, case_sensitive);

  if (json_route.hasObject("runtime")) {
    BaseJson::translateRuntimeUInt32(*json_route.getObject("runtime"), *match->mutable_runtime());
  }

  for (const auto json_header_matcher : json_route.getObjectArray("headers", true)) {
    auto* header_matcher = match->mutable_headers()->Add();
    translateHeaderMatcher(*json_header_matcher, *header_matcher);
  }

  for (const auto json_query_parameter_matcher :
       json_route.getObjectArray("query_parameters", true)) {
    auto* query_parameter_matcher = match->mutable_query_parameters()->Add();
    translateQueryParameterMatcher(*json_query_parameter_matcher, *query_parameter_matcher);
  }

  bool has_redirect = false;
  if (json_route.hasObject("host_redirect") || json_route.hasObject("path_redirect")) {
    has_redirect = true;
    auto* redirect = route.mutable_redirect();
    JSON_UTIL_SET_STRING(json_route, *redirect, host_redirect);
    JSON_UTIL_SET_STRING(json_route, *redirect, path_redirect);
    if (json_route.hasObject("use_websocket")) {
      throw EnvoyException("Redirect route entries must not have WebSockets set");
    }
  }
  const bool has_cluster = json_route.hasObject("cluster") ||
                           json_route.hasObject("cluster_header") ||
                           json_route.hasObject("weighted_clusters");

  if (has_cluster && has_redirect) {
    throw EnvoyException("routes must be either redirects or cluster targets");
  } else if (!has_cluster && !has_redirect) {
    throw EnvoyException(
        "routes must have redirect or one of cluster/cluster_header/weighted_clusters");
  } else if (has_cluster) {
    auto* action = route.mutable_route();

    if (json_route.hasObject("cluster")) {
      JSON_UTIL_SET_STRING(json_route, *action, cluster);
    } else if (json_route.hasObject("cluster_header")) {
      JSON_UTIL_SET_STRING(json_route, *action, cluster_header);
    } else {
      ASSERT(json_route.hasObject("weighted_clusters"));
      translateWeightedCluster(*json_route.getObject("weighted_clusters"),
                               *action->mutable_weighted_clusters());
    }

    // This is a trick to do a three-way XOR. It would be nice if we could do this with the JSON
    // schema but there is no obvious way to do this.
    if ((json_route.hasObject("cluster") + json_route.hasObject("cluster_header") +
         json_route.hasObject("weighted_clusters")) != 1) {
      throw EnvoyException("routes must specify one of cluster/cluster_header/weighted_clusters");
    }

    JSON_UTIL_SET_STRING(json_route, *action, prefix_rewrite);

    if (json_route.hasObject("host_rewrite")) {
      JSON_UTIL_SET_STRING(json_route, *action, host_rewrite);
      if (json_route.hasObject("auto_host_rewrite")) {
        throw EnvoyException(
            "routes cannot have both auto_host_rewrite and host_rewrite options set");
      }
    }
    if (json_route.hasObject("auto_host_rewrite")) {
      JSON_UTIL_SET_BOOL(json_route, *action, auto_host_rewrite);
    }

    JSON_UTIL_SET_BOOL(json_route, *action, use_websocket);
    JSON_UTIL_SET_DURATION(json_route, *action, timeout);

    if (json_route.hasObject("retry_policy")) {
      auto* retry_policy = action->mutable_retry_policy();
      const auto json_retry_policy = json_route.getObject("retry_policy");
      JSON_UTIL_SET_STRING(*json_retry_policy, *retry_policy, retry_on);
      JSON_UTIL_SET_INTEGER(*json_retry_policy, *retry_policy, num_retries);
      JSON_UTIL_SET_DURATION(*json_retry_policy, *retry_policy, per_try_timeout);
    }

    if (json_route.hasObject("shadow")) {
      auto* request_mirror_policy = action->mutable_request_mirror_policy();
      const auto json_shadow = json_route.getObject("shadow");
      JSON_UTIL_SET_STRING(*json_shadow, *request_mirror_policy, cluster);
      JSON_UTIL_SET_STRING(*json_shadow, *request_mirror_policy, runtime_key);
    }

    envoy::api::v2::RoutingPriority priority{};
    RoutingPriority_Parse(StringUtil::toUpper(json_route.getString("priority", "default")),
                          &priority);
    action->set_priority(priority);

    for (const auto header_value : json_route.getObjectArray("request_headers_to_add", true)) {
      auto* header_value_option = action->mutable_request_headers_to_add()->Add();
      BaseJson::translateHeaderValueOption(*header_value, *header_value_option);
    }

    for (const auto json_rate_limit : json_route.getObjectArray("rate_limits", true)) {
      auto* rate_limit = action->mutable_rate_limits()->Add();
      translateRateLimit(*json_rate_limit, *rate_limit);
    }

    JSON_UTIL_SET_BOOL(json_route, *action, include_vh_rate_limits);

    if (json_route.hasObject("hash_policy")) {
      const std::string header_name = json_route.getObject("hash_policy")->getString("header_name");
      action->mutable_hash_policy()->Add()->mutable_header()->set_header_name(header_name);
    }

    if (json_route.hasObject("cors")) {
      auto* cors = action->mutable_cors();
      const auto json_cors = json_route.getObject("cors");
      translateCors(*json_cors, *cors);
    }
  }

  if (json_route.hasObject("opaque_config")) {
    const Json::ObjectSharedPtr obj = json_route.getObject("opaque_config");
    auto& filter_metadata =
        (*route.mutable_metadata()->mutable_filter_metadata())[HttpFilterNames::get().ROUTER];
    obj->iterate([&filter_metadata](const std::string& name, const Json::Object& value) {
      (*filter_metadata.mutable_fields())[name].set_string_value(value.asString());
      return true;
    });
  }

  if (json_route.hasObject("decorator")) {
    auto* decorator = route.mutable_decorator();
    translateDecorator(*json_route.getObject("decorator"), *decorator);
  }
}

} // namespace Config
} // namespace Envoy
