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
  const std::string Buffer = "envoy.filters.http.buffer";
  // Cache filter
  const std::string Cache = "envoy.filters.http.cache";
  // CDN Loop filter
  const std::string CdnLoop = "envoy.filters.http.cdn_loop";
  // Compressor filter
  const std::string Compressor = "envoy.filters.http.compressor";
  // CORS filter
  const std::string Cors = "envoy.filters.http.cors";
  // CSRF filter
  const std::string Csrf = "envoy.filters.http.csrf";
  // Decompressor filter
  const std::string Decompressor = "envoy.filters.http.decompressor";
  // Dynamo filter
  const std::string Dynamo = "envoy.filters.http.dynamo";
  // Fault filter
  const std::string Fault = "envoy.filters.http.fault";
  // GRPC http1 bridge filter
  const std::string GrpcHttp1Bridge = "envoy.filters.http.grpc_http1_bridge";
  // GRPC json transcoder filter
  const std::string GrpcJsonTranscoder = "envoy.filters.http.grpc_json_transcoder";
  // GRPC web filter
  const std::string GrpcWeb = "envoy.filters.http.grpc_web";
  // GRPC http1 reverse bridge filter
  const std::string GrpcHttp1ReverseBridge = "envoy.filters.http.grpc_http1_reverse_bridge";
  // GRPC telemetry
  const std::string GrpcStats = "envoy.filters.http.grpc_stats";
  // Gzip filter
  const std::string EnvoyGzip = "envoy.filters.http.gzip";
  // IP tagging filter
  const std::string IpTagging = "envoy.filters.http.ip_tagging";
  // Rate limit filter
  const std::string RateLimit = "envoy.filters.http.ratelimit";
  // Router filter
  const std::string Router = "envoy.filters.http.router";
  // Health checking filter
  const std::string HealthCheck = "envoy.filters.http.health_check";
  // Lua filter
  const std::string Lua = "envoy.filters.http.lua";
  // On-demand RDS updates filter
  const std::string OnDemand = "envoy.filters.http.on_demand";
  // Squash filter
  const std::string Squash = "envoy.filters.http.squash";
  // External Authorization filter
  const std::string ExtAuthorization = "envoy.filters.http.ext_authz";
  // RBAC HTTP Authorization filter
  const std::string Rbac = "envoy.filters.http.rbac";
  // JWT authentication filter
  const std::string JwtAuthn = "envoy.filters.http.jwt_authn";
  // Header to metadata filter
  const std::string HeaderToMetadata = "envoy.filters.http.header_to_metadata";
  // Tap filter
  const std::string Tap = "envoy.filters.http.tap";
  // Adaptive concurrency limit filter
  const std::string AdaptiveConcurrency = "envoy.filters.http.adaptive_concurrency";
  // Admission control filter
  const std::string AdmissionControl = "envoy.filters.http.admission_control";
  // Original Src Filter
  const std::string OriginalSrc = "envoy.filters.http.original_src";
  // Dynamic forward proxy filter
  const std::string DynamicForwardProxy = "envoy.filters.http.dynamic_forward_proxy";
  // WebAssembly filter
  const std::string Wasm = "envoy.filters.http.wasm";
  // AWS request signing filter
  const std::string AwsRequestSigning = "envoy.filters.http.aws_request_signing";
  // AWS Lambda filter
  const std::string AwsLambda = "envoy.filters.http.aws_lambda";
  // OAuth filter
  const std::string OAuth = "envoy.filters.http.oauth2";
};

using HttpFilterNames = ConstSingleton<HttpFilterNameValues>;

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
