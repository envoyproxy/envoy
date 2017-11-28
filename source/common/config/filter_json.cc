#include "common/config/filter_json.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/address_json.h"
#include "common/config/json_utility.h"
#include "common/config/protocol_json.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

namespace {

void translateComparisonFilter(const Json::Object& config,
                               envoy::api::v2::filter::accesslog::ComparisonFilter& filter) {
  const std::string op = config.getString("op");
  if (op == ">=") {
    filter.set_op(envoy::api::v2::filter::accesslog::ComparisonFilter::GE);
  } else {
    ASSERT(op == "=");
    filter.set_op(envoy::api::v2::filter::accesslog::ComparisonFilter::EQ);
  }

  auto* runtime = filter.mutable_value();
  runtime->set_default_value(config.getInteger("value"));
  runtime->set_runtime_key(config.getString("runtime_key", ""));
}

void translateStatusCodeFilter(const Json::Object& config,
                               envoy::api::v2::filter::accesslog::StatusCodeFilter& filter) {
  translateComparisonFilter(config, *filter.mutable_comparison());
}

void translateDurationFilter(const Json::Object& config,
                             envoy::api::v2::filter::accesslog::DurationFilter& filter) {
  translateComparisonFilter(config, *filter.mutable_comparison());
}

void translateRuntimeFilter(const Json::Object& config,
                            envoy::api::v2::filter::accesslog::RuntimeFilter& filter) {
  filter.set_runtime_key(config.getString("key"));
}

void translateRepeatedFilter(
    const Json::Object& config,
    Protobuf::RepeatedPtrField<envoy::api::v2::filter::accesslog::AccessLogFilter>& filters) {
  for (const auto& json_filter : config.getObjectArray("filters")) {
    FilterJson::translateAccessLogFilter(*json_filter, *filters.Add());
  }
}

void translateOrFilter(const Json::Object& config,
                       envoy::api::v2::filter::accesslog::OrFilter& filter) {
  translateRepeatedFilter(config, *filter.mutable_filters());
}

void translateAndFilter(const Json::Object& config,
                        envoy::api::v2::filter::accesslog::AndFilter& filter) {
  translateRepeatedFilter(config, *filter.mutable_filters());
}

void translateRepeatedAccessLog(
    const std::vector<Json::ObjectSharedPtr>& json,
    Protobuf::RepeatedPtrField<envoy::api::v2::filter::accesslog::AccessLog>& access_logs) {
  for (const auto& json_access_log : json) {
    auto* access_log = access_logs.Add();
    FilterJson::translateAccessLog(*json_access_log, *access_log);
  }
}

} // namespace

void FilterJson::translateAccessLogFilter(
    const Json::Object& json_access_log_filter,
    envoy::api::v2::filter::accesslog::AccessLogFilter& access_log_filter) {
  const std::string type = json_access_log_filter.getString("type");
  if (type == "status_code") {
    translateStatusCodeFilter(json_access_log_filter,
                              *access_log_filter.mutable_status_code_filter());
  } else if (type == "duration") {
    translateDurationFilter(json_access_log_filter, *access_log_filter.mutable_duration_filter());
  } else if (type == "runtime") {
    translateRuntimeFilter(json_access_log_filter, *access_log_filter.mutable_runtime_filter());
  } else if (type == "logical_or") {
    translateOrFilter(json_access_log_filter, *access_log_filter.mutable_or_filter());
  } else if (type == "logical_and") {
    translateAndFilter(json_access_log_filter, *access_log_filter.mutable_and_filter());
  } else if (type == "not_healthcheck") {
    access_log_filter.mutable_not_health_check_filter();
  } else {
    ASSERT(type == "traceable_request");
    access_log_filter.mutable_traceable_filter();
  }
}

void FilterJson::translateAccessLog(const Json::Object& json_access_log,
                                    envoy::api::v2::filter::accesslog::AccessLog& access_log) {
  json_access_log.validateSchema(Json::Schema::ACCESS_LOG_SCHEMA);

  envoy::api::v2::filter::accesslog::FileAccessLog file_access_log;

  JSON_UTIL_SET_STRING(json_access_log, file_access_log, path);
  JSON_UTIL_SET_STRING(json_access_log, file_access_log, format);

  ProtobufWkt::Struct& custom_config = *access_log.mutable_config();
  MessageUtil::jsonConvert(file_access_log, custom_config);

  // Statically registered access logs are a v2-only feature, so use the standard internal file
  // access log for json config conversion.
  access_log.set_name(Config::AccessLogNames::get().FILE);

  if (json_access_log.hasObject("filter")) {
    translateAccessLogFilter(*json_access_log.getObject("filter"), *access_log.mutable_filter());
  }
}

void FilterJson::translateHttpConnectionManager(
    const Json::Object& json_http_connection_manager,
    envoy::api::v2::filter::network::HttpConnectionManager& http_connection_manager) {
  json_http_connection_manager.validateSchema(Json::Schema::HTTP_CONN_NETWORK_FILTER_SCHEMA);

  envoy::api::v2::filter::network::HttpConnectionManager::CodecType codec_type{};
  envoy::api::v2::filter::network::HttpConnectionManager::CodecType_Parse(
      StringUtil::toUpper(json_http_connection_manager.getString("codec_type")), &codec_type);
  http_connection_manager.set_codec_type(codec_type);

  JSON_UTIL_SET_STRING(json_http_connection_manager, http_connection_manager, stat_prefix);

  if (json_http_connection_manager.hasObject("rds")) {
    Utility::translateRdsConfig(*json_http_connection_manager.getObject("rds"),
                                *http_connection_manager.mutable_rds());
  }
  if (json_http_connection_manager.hasObject("route_config")) {
    if (json_http_connection_manager.hasObject("rds")) {
      throw EnvoyException(
          "http connection manager must have either rds or route_config but not both");
    }
    RdsJson::translateRouteConfiguration(*json_http_connection_manager.getObject("route_config"),
                                         *http_connection_manager.mutable_route_config());
  }

  for (const auto& json_filter : json_http_connection_manager.getObjectArray("filters", true)) {
    auto* filter = http_connection_manager.mutable_http_filters()->Add();
    JSON_UTIL_SET_STRING(*json_filter, *filter, name);

    // Translate v1 name to v2 name.
    filter->set_name(
        Config::HttpFilterNames::get().v1_converter_.getV2Name(json_filter->getString("name")));
    JSON_UTIL_SET_STRING(*json_filter, *filter->mutable_deprecated_v1(), type);

    const std::string json_config = "{\"deprecated_v1\": true, \"value\": " +
                                    json_filter->getObject("config")->asJsonString() + "}";

    const auto status = Protobuf::util::JsonStringToMessage(json_config, filter->mutable_config());
    // JSON schema has already validated that this is a valid JSON object.
    ASSERT(status.ok());
    UNREFERENCED_PARAMETER(status);
  }

  JSON_UTIL_SET_BOOL(json_http_connection_manager, http_connection_manager, add_user_agent);

  if (json_http_connection_manager.hasObject("tracing")) {
    const auto json_tracing = json_http_connection_manager.getObject("tracing");
    auto* tracing = http_connection_manager.mutable_tracing();

    envoy::api::v2::filter::network::HttpConnectionManager::Tracing::OperationName operation_name{};
    envoy::api::v2::filter::network::HttpConnectionManager::Tracing::OperationName_Parse(
        StringUtil::toUpper(json_tracing->getString("operation_name")), &operation_name);
    tracing->set_operation_name(operation_name);

    for (const std::string& header :
         json_tracing->getStringArray("request_headers_for_tags", true)) {
      tracing->add_request_headers_for_tags(header);
    }
  }

  if (json_http_connection_manager.hasObject("http1_settings")) {
    ProtocolJson::translateHttp1ProtocolOptions(
        *json_http_connection_manager.getObject("http1_settings"),
        *http_connection_manager.mutable_http_protocol_options());
  }

  if (json_http_connection_manager.hasObject("http2_settings")) {
    ProtocolJson::translateHttp2ProtocolOptions(
        *json_http_connection_manager.getObject("http2_settings"),
        *http_connection_manager.mutable_http2_protocol_options());
  }

  JSON_UTIL_SET_STRING(json_http_connection_manager, http_connection_manager, server_name);
  JSON_UTIL_SET_DURATION_SECONDS(json_http_connection_manager, http_connection_manager,
                                 idle_timeout);
  JSON_UTIL_SET_DURATION(json_http_connection_manager, http_connection_manager, drain_timeout);

  translateRepeatedAccessLog(json_http_connection_manager.getObjectArray("access_log", true),
                             *http_connection_manager.mutable_access_log());

  JSON_UTIL_SET_BOOL(json_http_connection_manager, http_connection_manager, use_remote_address);
  JSON_UTIL_SET_BOOL(json_http_connection_manager, http_connection_manager, generate_request_id);

  envoy::api::v2::filter::network::HttpConnectionManager::ForwardClientCertDetails fcc_details{};
  envoy::api::v2::filter::network::HttpConnectionManager::ForwardClientCertDetails_Parse(
      StringUtil::toUpper(
          json_http_connection_manager.getString("forward_client_cert", "sanitize")),
      &fcc_details);
  http_connection_manager.set_forward_client_cert_details(fcc_details);

  for (const std::string& detail :
       json_http_connection_manager.getStringArray("set_current_client_cert_details", true)) {
    if (detail == "Subject") {
      http_connection_manager.mutable_set_current_client_cert_details()
          ->mutable_subject()
          ->set_value(true);
    } else {
      ASSERT(detail == "SAN");
      http_connection_manager.mutable_set_current_client_cert_details()->mutable_san()->set_value(
          true);
    }
  }
}

void FilterJson::translateRedisProxy(const Json::Object& json_redis_proxy,
                                     envoy::api::v2::filter::network::RedisProxy& redis_proxy) {
  json_redis_proxy.validateSchema(Json::Schema::REDIS_PROXY_NETWORK_FILTER_SCHEMA);
  JSON_UTIL_SET_STRING(json_redis_proxy, redis_proxy, stat_prefix);
  redis_proxy.set_cluster(json_redis_proxy.getString("cluster_name"));

  const auto json_conn_pool = json_redis_proxy.getObject("conn_pool");
  json_conn_pool->validateSchema(Json::Schema::REDIS_CONN_POOL_SCHEMA);

  auto* conn_pool = redis_proxy.mutable_settings();
  JSON_UTIL_SET_DURATION(*json_conn_pool, *conn_pool, op_timeout);
}

void FilterJson::translateMongoProxy(const Json::Object& json_mongo_proxy,
                                     envoy::api::v2::filter::network::MongoProxy& mongo_proxy) {
  json_mongo_proxy.validateSchema(Json::Schema::MONGO_PROXY_NETWORK_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_mongo_proxy, mongo_proxy, stat_prefix);
  JSON_UTIL_SET_STRING(json_mongo_proxy, mongo_proxy, access_log);
  if (json_mongo_proxy.hasObject("fault")) {
    const auto json_fault = json_mongo_proxy.getObject("fault")->getObject("fixed_delay");
    auto* delay = mongo_proxy.mutable_delay();

    delay->set_type(envoy::api::v2::filter::FaultDelay::FIXED);
    delay->set_percent(static_cast<uint32_t>(json_fault->getInteger("percent")));
    JSON_UTIL_SET_DURATION_FROM_FIELD(*json_fault, *delay, fixed_delay, duration);
  }
}

void FilterJson::translateFaultFilter(const Json::Object& json_fault,
                                      envoy::api::v2::filter::http::HTTPFault& fault) {
  json_fault.validateSchema(Json::Schema::FAULT_HTTP_FILTER_SCHEMA);

  const Json::ObjectSharedPtr config_abort = json_fault.getObject("abort", true);
  const Json::ObjectSharedPtr config_delay = json_fault.getObject("delay", true);

  if (!config_abort->empty()) {
    auto* abort_fault = fault.mutable_abort();
    abort_fault->set_percent(static_cast<uint32_t>(config_abort->getInteger("abort_percent")));

    // TODO(mattklein123): Throw error if invalid return code is provided
    abort_fault->set_http_status(static_cast<uint32_t>(config_abort->getInteger("http_status")));
  }

  if (!config_delay->empty()) {
    auto* delay = fault.mutable_delay();
    delay->set_type(envoy::api::v2::filter::FaultDelay::FIXED);
    delay->set_percent(static_cast<uint32_t>(config_delay->getInteger("fixed_delay_percent")));
    JSON_UTIL_SET_DURATION_FROM_FIELD(*config_delay, *delay, fixed_delay, fixed_duration);
  }

  for (const auto json_header_matcher : json_fault.getObjectArray("headers", true)) {
    auto* header_matcher = fault.mutable_headers()->Add();
    RdsJson::translateHeaderMatcher(*json_header_matcher, *header_matcher);
  }

  JSON_UTIL_SET_STRING(json_fault, fault, upstream_cluster);

  for (auto json_downstream_node : json_fault.getStringArray("downstream_nodes", true)) {
    auto* downstream_node = fault.mutable_downstream_nodes()->Add();
    *downstream_node = json_downstream_node;
  }
}

void FilterJson::translateHealthCheckFilter(
    const Json::Object& json_health_check,
    envoy::api::v2::filter::http::HealthCheck& health_check) {
  json_health_check.validateSchema(Json::Schema::HEALTH_CHECK_HTTP_FILTER_SCHEMA);

  JSON_UTIL_SET_BOOL(json_health_check, health_check, pass_through_mode);
  JSON_UTIL_SET_DURATION(json_health_check, health_check, cache_time);
  JSON_UTIL_SET_STRING(json_health_check, health_check, endpoint);
}

void FilterJson::translateRouter(const Json::Object& json_router,
                                 envoy::api::v2::filter::http::Router& router) {
  json_router.validateSchema(Json::Schema::ROUTER_HTTP_FILTER_SCHEMA);

  router.mutable_dynamic_stats()->set_value(json_router.getBoolean("dynamic_stats", true));
  router.set_start_child_span(json_router.getBoolean("start_child_span", false));
}

void FilterJson::translateBufferFilter(const Json::Object& json_buffer,
                                       envoy::api::v2::filter::http::Buffer& buffer) {
  json_buffer.validateSchema(Json::Schema::BUFFER_HTTP_FILTER_SCHEMA);

  JSON_UTIL_SET_INTEGER(json_buffer, buffer, max_request_bytes);
  JSON_UTIL_SET_DURATION_SECONDS(json_buffer, buffer, max_request_time);
}

void FilterJson::translateTcpProxy(const Json::Object& json_tcp_proxy,
                                   envoy::api::v2::filter::network::TcpProxy& tcp_proxy) {
  json_tcp_proxy.validateSchema(Json::Schema::TCP_PROXY_NETWORK_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_tcp_proxy, tcp_proxy, stat_prefix);
  translateRepeatedAccessLog(json_tcp_proxy.getObjectArray("access_log", true),
                             *tcp_proxy.mutable_access_log());

  for (const Json::ObjectSharedPtr& route_desc :
       json_tcp_proxy.getObject("route_config")->getObjectArray("routes")) {
    envoy::api::v2::filter::network::TcpProxy::DeprecatedV1::TCPRoute* route =
        tcp_proxy.mutable_deprecated_v1()->mutable_routes()->Add();
    JSON_UTIL_SET_STRING(*route_desc, *route, cluster);
    JSON_UTIL_SET_STRING(*route_desc, *route, destination_ports);
    JSON_UTIL_SET_STRING(*route_desc, *route, source_ports);
    AddressJson::translateCidrRangeList(route_desc->getStringArray("source_ip_list", true),
                                        *route->mutable_source_ip_list());
    AddressJson::translateCidrRangeList(route_desc->getStringArray("destination_ip_list", true),
                                        *route->mutable_destination_ip_list());
  }
}

} // namespace Config
} // namespace Envoy
