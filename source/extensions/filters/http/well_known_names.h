#pragma once

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

/**
 * Well-known http filter names.
 * NOTE: New filters should use the well known name: envoy.filters.http.name.
 */
class HttpFilterNameValues {
public:
  // Buffer filter
  const std::string BUFFER = "envoy.buffer";
  // CORS filter
  const std::string CORS = "envoy.cors";
  // Dynamo filter
  const std::string DYNAMO = "envoy.http_dynamo_filter";
  // Fault filter
  const std::string FAULT = "envoy.fault";
  // GRPC http1 bridge filter
  const std::string GRPC_HTTP1_BRIDGE = "envoy.grpc_http1_bridge";
  // GRPC json transcoder filter
  const std::string GRPC_JSON_TRANSCODER = "envoy.grpc_json_transcoder";
  // GRPC web filter
  const std::string GRPC_WEB = "envoy.grpc_web";
  // Gzip filter
  const std::string ENVOY_GZIP = "envoy.gzip";
  // IP tagging filter
  const std::string IP_TAGGING = "envoy.ip_tagging";
  // Rate limit filter
  const std::string RATE_LIMIT = "envoy.rate_limit";
  // Router filter
  const std::string ROUTER = "envoy.router";
  // Health checking filter
  const std::string HEALTH_CHECK = "envoy.health_check";
  // Lua filter
  const std::string LUA = "envoy.lua";
  // Squash filter
  const std::string SQUASH = "envoy.squash";
  // External Authorization filter
  const std::string EXT_AUTHORIZATION = "envoy.ext_authz";

  // Converts names from v1 to v2
  const Config::V1Converter v1_converter_;

  // NOTE: Do not add any new filters to this list. All future filters are v2 only.
  HttpFilterNameValues()
      : v1_converter_({BUFFER, CORS, DYNAMO, FAULT, GRPC_HTTP1_BRIDGE, GRPC_JSON_TRANSCODER,
                       GRPC_WEB, HEALTH_CHECK, IP_TAGGING, RATE_LIMIT, ROUTER, LUA,
                       EXT_AUTHORIZATION}) {}
};

typedef ConstSingleton<HttpFilterNameValues> HttpFilterNames;

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
