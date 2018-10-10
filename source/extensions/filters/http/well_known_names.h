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
  const std::string Buffer = "envoy.buffer";
  // CORS filter
  const std::string Cors = "envoy.cors";
  // Dynamo filter
  const std::string Dynamo = "envoy.http_dynamo_filter";
  // Fault filter
  const std::string Fault = "envoy.fault";
  // GRPC http1 bridge filter
  const std::string GrpcHttp1Bridge = "envoy.grpc_http1_bridge";
  // GRPC json transcoder filter
  const std::string GrpcJsonTranscoder = "envoy.grpc_json_transcoder";
  // GRPC web filter
  const std::string GrpcWeb = "envoy.grpc_web";
  // Gzip filter
  const std::string EnvoyGzip = "envoy.gzip";
  // IP tagging filter
  const std::string IpTagging = "envoy.ip_tagging";
  // Rate limit filter
  const std::string RateLimit = "envoy.rate_limit";
  // Router filter
  const std::string Router = "envoy.router";
  // Health checking filter
  const std::string HealthCheck = "envoy.health_check";
  // Lua filter
  const std::string Lua = "envoy.lua";
  // Squash filter
  const std::string Squash = "envoy.squash";
  // External Authorization filter
  const std::string ExtAuthorization = "envoy.ext_authz";
  // RBAC HTTP Authorization filter
  const std::string Rbac = "envoy.filters.http.rbac";
  // JWT authentication filter
  const std::string JwtAuthn = "envoy.filters.http.jwt_authn";
  // Header to metadata filter
  const std::string HeaderToMetadata = "envoy.filters.http.header_to_metadata";

  // Converts names from v1 to v2
  const Config::V1Converter v1_converter_;

  // NOTE: Do not add any new filters to this list. All future filters are v2 only.
  HttpFilterNameValues()
      : v1_converter_({Buffer, Cors, Dynamo, Fault, GrpcHttp1Bridge, GrpcJsonTranscoder, GrpcWeb,
                       HeaderToMetadata, HealthCheck, IpTagging, RateLimit, Router, Lua,
                       ExtAuthorization}) {}
};

typedef ConstSingleton<HttpFilterNameValues> HttpFilterNames;

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
