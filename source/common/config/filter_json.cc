#include "common/config/filter_json.h"

#include "envoy/config/accesslog/v2/file.pb.h"
#include "envoy/stats/stats_options.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/address_json.h"
#include "common/config/json_utility.h"
#include "common/config/protocol_json.h"
#include "common/config/rds_json.h"
#include "common/config/utility.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/access_loggers/well_known_names.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Config {

namespace {

void translateComparisonFilter(const Json::Object& json_config,
                               envoy::config::filter::accesslog::v2::ComparisonFilter& filter) {
  const std::string op = json_config.getString("op");
  if (op == ">=") {
    filter.set_op(envoy::config::filter::accesslog::v2::ComparisonFilter::GE);
  } else {
    ASSERT(op == "=");
    filter.set_op(envoy::config::filter::accesslog::v2::ComparisonFilter::EQ);
  }

  auto* runtime = filter.mutable_value();
  runtime->set_default_value(json_config.getInteger("value"));
  runtime->set_runtime_key(json_config.getString("runtime_key", ""));
}

void translateStatusCodeFilter(const Json::Object& json_config,
                               envoy::config::filter::accesslog::v2::StatusCodeFilter& filter) {
  translateComparisonFilter(json_config, *filter.mutable_comparison());
}

void translateDurationFilter(const Json::Object& json_config,
                             envoy::config::filter::accesslog::v2::DurationFilter& filter) {
  translateComparisonFilter(json_config, *filter.mutable_comparison());
}

void translateRuntimeFilter(const Json::Object& json_config,
                            envoy::config::filter::accesslog::v2::RuntimeFilter& filter) {
  filter.set_runtime_key(json_config.getString("key"));
}

void translateRepeatedFilter(
    const Json::Object& json_config,
    Protobuf::RepeatedPtrField<envoy::config::filter::accesslog::v2::AccessLogFilter>& filters) {
  for (const auto& json_filter : json_config.getObjectArray("filters")) {
    FilterJson::translateAccessLogFilter(*json_filter, *filters.Add());
  }
}

void translateOrFilter(const Json::Object& json_config,
                       envoy::config::filter::accesslog::v2::OrFilter& filter) {
  translateRepeatedFilter(json_config, *filter.mutable_filters());
}

void translateAndFilter(const Json::Object& json_config,
                        envoy::config::filter::accesslog::v2::AndFilter& filter) {
  translateRepeatedFilter(json_config, *filter.mutable_filters());
}

void translateRepeatedAccessLog(
    const std::vector<Json::ObjectSharedPtr>& json,
    Protobuf::RepeatedPtrField<envoy::config::filter::accesslog::v2::AccessLog>& access_logs) {
  for (const auto& json_access_log : json) {
    auto* access_log = access_logs.Add();
    FilterJson::translateAccessLog(*json_access_log, *access_log);
  }
}

} // namespace

void FilterJson::translateAccessLogFilter(
    const Json::Object& json_config,
    envoy::config::filter::accesslog::v2::AccessLogFilter& proto_config) {
  const std::string type = json_config.getString("type");
  if (type == "status_code") {
    translateStatusCodeFilter(json_config, *proto_config.mutable_status_code_filter());
  } else if (type == "duration") {
    translateDurationFilter(json_config, *proto_config.mutable_duration_filter());
  } else if (type == "runtime") {
    translateRuntimeFilter(json_config, *proto_config.mutable_runtime_filter());
  } else if (type == "logical_or") {
    translateOrFilter(json_config, *proto_config.mutable_or_filter());
  } else if (type == "logical_and") {
    translateAndFilter(json_config, *proto_config.mutable_and_filter());
  } else if (type == "not_healthcheck") {
    proto_config.mutable_not_health_check_filter();
  } else {
    ASSERT(type == "traceable_request");
    proto_config.mutable_traceable_filter();
  }
}

void FilterJson::translateAccessLog(const Json::Object& json_config,
                                    envoy::config::filter::accesslog::v2::AccessLog& proto_config) {
  json_config.validateSchema(Json::Schema::ACCESS_LOG_SCHEMA);

  envoy::config::accesslog::v2::FileAccessLog file_access_log;

  JSON_UTIL_SET_STRING(json_config, file_access_log, path);
  JSON_UTIL_SET_STRING(json_config, file_access_log, format);

  ProtobufWkt::Struct& custom_config = *proto_config.mutable_config();
  MessageUtil::jsonConvert(file_access_log, custom_config);

  // Statically registered access logs are a v2-only feature, so use the standard internal file
  // access log for json config conversion.
  proto_config.set_name(Extensions::AccessLoggers::AccessLogNames::get().File);

  if (json_config.hasObject("filter")) {
    translateAccessLogFilter(*json_config.getObject("filter"), *proto_config.mutable_filter());
  }
}

void FilterJson::translateHttpConnectionManager(
    const Json::Object& json_config,
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        proto_config,
    const Stats::StatsOptions& stats_options) {
  json_config.validateSchema(Json::Schema::HTTP_CONN_NETWORK_FILTER_SCHEMA);

  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::CodecType
      codec_type{};
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      CodecType_Parse(StringUtil::toUpper(json_config.getString("codec_type")), &codec_type);
  proto_config.set_codec_type(codec_type);

  JSON_UTIL_SET_STRING(json_config, proto_config, stat_prefix);

  if (json_config.hasObject("rds")) {
    Utility::translateRdsConfig(*json_config.getObject("rds"), *proto_config.mutable_rds(),
                                stats_options);
  }
  if (json_config.hasObject("route_config")) {
    if (json_config.hasObject("rds")) {
      throw EnvoyException(
          "http connection manager must have either rds or route_config but not both");
    }
    RdsJson::translateRouteConfiguration(*json_config.getObject("route_config"),
                                         *proto_config.mutable_route_config(), stats_options);
  }

  for (const auto& json_filter : json_config.getObjectArray("filters", true)) {
    auto* filter = proto_config.mutable_http_filters()->Add();
    JSON_UTIL_SET_STRING(*json_filter, *filter, name);

    // Translate v1 name to v2 name.
    filter->set_name(Extensions::HttpFilters::HttpFilterNames::get().v1_converter_.getV2Name(
        json_filter->getString("name")));
    JSON_UTIL_SET_STRING(*json_filter, *filter->mutable_deprecated_v1(), type);

    const std::string deprecated_config =
        "{\"deprecated_v1\": true, \"value\": " + json_filter->getObject("config")->asJsonString() +
        "}";

    const auto status =
        Protobuf::util::JsonStringToMessage(deprecated_config, filter->mutable_config());
    // JSON schema has already validated that this is a valid JSON object.
    ASSERT(status.ok());
  }

  JSON_UTIL_SET_BOOL(json_config, proto_config, add_user_agent);

  if (json_config.hasObject("tracing")) {
    const auto json_tracing = json_config.getObject("tracing");
    auto* tracing = proto_config.mutable_tracing();

    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::Tracing::
        OperationName operation_name{};
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::Tracing::
        OperationName_Parse(StringUtil::toUpper(json_tracing->getString("operation_name")),
                            &operation_name);
    tracing->set_operation_name(operation_name);

    for (const std::string& header :
         json_tracing->getStringArray("request_headers_for_tags", true)) {
      tracing->add_request_headers_for_tags(header);
    }
  }

  if (json_config.hasObject("http1_settings")) {
    ProtocolJson::translateHttp1ProtocolOptions(*json_config.getObject("http1_settings"),
                                                *proto_config.mutable_http_protocol_options());
  }

  if (json_config.hasObject("http2_settings")) {
    ProtocolJson::translateHttp2ProtocolOptions(*json_config.getObject("http2_settings"),
                                                *proto_config.mutable_http2_protocol_options());
  }

  JSON_UTIL_SET_STRING(json_config, proto_config, server_name);
  JSON_UTIL_SET_DURATION_SECONDS(json_config, proto_config, idle_timeout);
  JSON_UTIL_SET_DURATION(json_config, proto_config, drain_timeout);

  translateRepeatedAccessLog(json_config.getObjectArray("access_log", true),
                             *proto_config.mutable_access_log());

  JSON_UTIL_SET_BOOL(json_config, proto_config, use_remote_address);
  JSON_UTIL_SET_BOOL(json_config, proto_config, generate_request_id);

  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      ForwardClientCertDetails fcc_details{};
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      ForwardClientCertDetails_Parse(
          StringUtil::toUpper(json_config.getString("forward_client_cert", "sanitize")),
          &fcc_details);
  proto_config.set_forward_client_cert_details(fcc_details);

  for (const std::string& detail :
       json_config.getStringArray("set_current_client_cert_details", true)) {
    if (detail == "Subject") {
      proto_config.mutable_set_current_client_cert_details()->mutable_subject()->set_value(true);
    } else {
      ASSERT(detail == "SAN");
      proto_config.mutable_set_current_client_cert_details()->set_uri(true);
    }
  }
}

void FilterJson::translateRedisProxy(
    const Json::Object& json_config,
    envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config) {
  json_config.validateSchema(Json::Schema::REDIS_PROXY_NETWORK_FILTER_SCHEMA);
  JSON_UTIL_SET_STRING(json_config, proto_config, stat_prefix);
  proto_config.set_cluster(json_config.getString("cluster_name"));

  const auto json_conn_pool = json_config.getObject("conn_pool");
  json_conn_pool->validateSchema(Json::Schema::REDIS_CONN_POOL_SCHEMA);

  auto* conn_pool = proto_config.mutable_settings();
  JSON_UTIL_SET_DURATION(*json_conn_pool, *conn_pool, op_timeout);
}

void FilterJson::translateMongoProxy(
    const Json::Object& json_config,
    envoy::config::filter::network::mongo_proxy::v2::MongoProxy& proto_config) {
  json_config.validateSchema(Json::Schema::MONGO_PROXY_NETWORK_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_config, proto_config, stat_prefix);
  JSON_UTIL_SET_STRING(json_config, proto_config, access_log);
  if (json_config.hasObject("fault")) {
    const auto json_fault = json_config.getObject("fault")->getObject("fixed_delay");
    auto* delay = proto_config.mutable_delay();

    delay->set_type(envoy::config::filter::fault::v2::FaultDelay::FIXED);
    delay->set_percent(static_cast<uint32_t>(json_fault->getInteger("percent")));
    JSON_UTIL_SET_DURATION_FROM_FIELD(*json_fault, *delay, fixed_delay, duration);
  }
}

void FilterJson::translateFaultFilter(
    const Json::Object& json_config,
    envoy::config::filter::http::fault::v2::HTTPFault& proto_config) {
  json_config.validateSchema(Json::Schema::FAULT_HTTP_FILTER_SCHEMA);

  const Json::ObjectSharedPtr json_config_abort = json_config.getObject("abort", true);
  const Json::ObjectSharedPtr json_config_delay = json_config.getObject("delay", true);

  if (!json_config_abort->empty()) {
    auto* abort_fault = proto_config.mutable_abort();
    abort_fault->set_percent(static_cast<uint32_t>(json_config_abort->getInteger("abort_percent")));

    // TODO(mattklein123): Throw error if invalid return code is provided
    abort_fault->set_http_status(
        static_cast<uint32_t>(json_config_abort->getInteger("http_status")));
  }

  if (!json_config_delay->empty()) {
    auto* delay = proto_config.mutable_delay();
    delay->set_type(envoy::config::filter::fault::v2::FaultDelay::FIXED);
    delay->set_percent(static_cast<uint32_t>(json_config_delay->getInteger("fixed_delay_percent")));
    JSON_UTIL_SET_DURATION_FROM_FIELD(*json_config_delay, *delay, fixed_delay, fixed_duration);
  }

  for (const auto json_header_matcher : json_config.getObjectArray("headers", true)) {
    auto* header_matcher = proto_config.mutable_headers()->Add();
    RdsJson::translateHeaderMatcher(*json_header_matcher, *header_matcher);
  }

  JSON_UTIL_SET_STRING(json_config, proto_config, upstream_cluster);

  for (auto json_downstream_node : json_config.getStringArray("downstream_nodes", true)) {
    auto* downstream_node = proto_config.mutable_downstream_nodes()->Add();
    *downstream_node = json_downstream_node;
  }
}

void FilterJson::translateHealthCheckFilter(
    const Json::Object& json_config,
    envoy::config::filter::http::health_check::v2::HealthCheck& proto_config) {
  json_config.validateSchema(Json::Schema::HEALTH_CHECK_HTTP_FILTER_SCHEMA);

  JSON_UTIL_SET_BOOL(json_config, proto_config, pass_through_mode);
  JSON_UTIL_SET_DURATION(json_config, proto_config, cache_time);
  std::string endpoint = json_config.getString("endpoint");
  auto& header = *proto_config.add_headers();
  header.set_name(":path");
  header.set_exact_match(endpoint);
}

void FilterJson::translateGrpcJsonTranscoder(
    const Json::Object& json_config,
    envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder& proto_config) {
  json_config.validateSchema(Json::Schema::GRPC_JSON_TRANSCODER_FILTER_SCHEMA);
  JSON_UTIL_SET_STRING(json_config, proto_config, proto_descriptor);
  auto* services = proto_config.mutable_services();
  for (const auto& service_name : json_config.getStringArray("services")) {
    *services->Add() = service_name;
  }

  if (json_config.hasObject("print_options")) {
    auto json_print_options = json_config.getObject("print_options");
    auto* proto_print_options = proto_config.mutable_print_options();
    proto_print_options->set_add_whitespace(
        json_print_options->getBoolean("add_whitespace", false));
    proto_print_options->set_always_print_primitive_fields(
        json_print_options->getBoolean("always_print_primitive_fields", false));
    proto_print_options->set_always_print_enums_as_ints(
        json_print_options->getBoolean("always_print_enums_as_ints", false));
    proto_print_options->set_preserve_proto_field_names(
        json_print_options->getBoolean("preserve_proto_field_names", false));
  }
}

void FilterJson::translateSquashConfig(
    const Json::Object& json_config,
    envoy::config::filter::http::squash::v2::Squash& proto_config) {
  json_config.validateSchema(Json::Schema::SQUASH_HTTP_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_config, proto_config, cluster);
  // convert json object to google.protobuf.Struct
  std::string json_string = json_config.getObject("attachment_template")->asJsonString();
  MessageUtil::loadFromJson(json_string, *proto_config.mutable_attachment_template());

  JSON_UTIL_SET_DURATION(json_config, proto_config, attachment_timeout);
  JSON_UTIL_SET_DURATION(json_config, proto_config, attachment_poll_period);
  JSON_UTIL_SET_DURATION(json_config, proto_config, request_timeout);
}

void FilterJson::translateRouter(const Json::Object& json_config,
                                 envoy::config::filter::http::router::v2::Router& proto_config) {
  json_config.validateSchema(Json::Schema::ROUTER_HTTP_FILTER_SCHEMA);

  proto_config.mutable_dynamic_stats()->set_value(json_config.getBoolean("dynamic_stats", true));
  proto_config.set_start_child_span(json_config.getBoolean("start_child_span", false));
}

void FilterJson::translateBufferFilter(
    const Json::Object& json_config,
    envoy::config::filter::http::buffer::v2::Buffer& proto_config) {
  json_config.validateSchema(Json::Schema::BUFFER_HTTP_FILTER_SCHEMA);

  JSON_UTIL_SET_INTEGER(json_config, proto_config, max_request_bytes);
  JSON_UTIL_SET_DURATION_SECONDS(json_config, proto_config, max_request_time);
}

void FilterJson::translateLuaFilter(const Json::Object& json_config,
                                    envoy::config::filter::http::lua::v2::Lua& proto_config) {
  json_config.validateSchema(Json::Schema::LUA_HTTP_FILTER_SCHEMA);
  JSON_UTIL_SET_STRING(json_config, proto_config, inline_code);
}

void FilterJson::translateTcpProxy(
    const Json::Object& json_config,
    envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config) {
  json_config.validateSchema(Json::Schema::TCP_PROXY_NETWORK_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_config, proto_config, stat_prefix);
  translateRepeatedAccessLog(json_config.getObjectArray("access_log", true),
                             *proto_config.mutable_access_log());

  for (const Json::ObjectSharedPtr& route_desc :
       json_config.getObject("route_config")->getObjectArray("routes")) {
    envoy::config::filter::network::tcp_proxy::v2::TcpProxy::DeprecatedV1::TCPRoute* route =
        proto_config.mutable_deprecated_v1()->mutable_routes()->Add();
    JSON_UTIL_SET_STRING(*route_desc, *route, cluster);
    JSON_UTIL_SET_STRING(*route_desc, *route, destination_ports);
    JSON_UTIL_SET_STRING(*route_desc, *route, source_ports);
    AddressJson::translateCidrRangeList(route_desc->getStringArray("source_ip_list", true),
                                        *route->mutable_source_ip_list());
    AddressJson::translateCidrRangeList(route_desc->getStringArray("destination_ip_list", true),
                                        *route->mutable_destination_ip_list());
  }
}

void FilterJson::translateTcpRateLimitFilter(
    const Json::Object& json_config,
    envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config) {
  json_config.validateSchema(Json::Schema::RATELIMIT_NETWORK_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_config, proto_config, stat_prefix);
  JSON_UTIL_SET_STRING(json_config, proto_config, domain);
  JSON_UTIL_SET_DURATION(json_config, proto_config, timeout);

  auto* descriptors = proto_config.mutable_descriptors();
  for (const auto& json_descriptor : json_config.getObjectArray("descriptors", false)) {
    auto* entries = descriptors->Add()->mutable_entries();
    for (const auto& json_entry : json_descriptor->asObjectArray()) {
      auto* entry = entries->Add();
      JSON_UTIL_SET_STRING(*json_entry, *entry, key);
      JSON_UTIL_SET_STRING(*json_entry, *entry, value);
    }
  }
}

void FilterJson::translateHttpRateLimitFilter(
    const Json::Object& json_config,
    envoy::config::filter::http::rate_limit::v2::RateLimit& proto_config) {
  json_config.validateSchema(Json::Schema::RATE_LIMIT_HTTP_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_config, proto_config, domain);
  proto_config.set_stage(json_config.getInteger("stage", 0));

  JSON_UTIL_SET_STRING(json_config, proto_config, request_type);
  JSON_UTIL_SET_DURATION(json_config, proto_config, timeout);
}

void FilterJson::translateClientSslAuthFilter(
    const Json::Object& json_config,
    envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& proto_config) {
  json_config.validateSchema(Json::Schema::CLIENT_SSL_NETWORK_FILTER_SCHEMA);

  JSON_UTIL_SET_STRING(json_config, proto_config, auth_api_cluster);
  JSON_UTIL_SET_STRING(json_config, proto_config, stat_prefix);
  JSON_UTIL_SET_DURATION(json_config, proto_config, refresh_delay);

  AddressJson::translateCidrRangeList(json_config.getStringArray("ip_white_list", true),
                                      *proto_config.mutable_ip_white_list());
}

} // namespace Config
} // namespace Envoy
