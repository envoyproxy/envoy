#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Runtime {

// Add additional features here to enable the new code paths by default.
//
// Per documentation in CONTRIBUTING.md is expected that new high risk code paths be guarded
// by runtime feature guards, i.e
//
// if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.my_feature_name")) {
//   [new code path]
// else {
//   [old_code_path]
// }
//
// Runtime features are false by default, so the old code path is exercised.
// To make a runtime feature true by default, add it to the array below.
// New features should be true-by-default for an Envoy release cycle before the
// old code path is removed.
//
// If issues are found that require a runtime feature to be disabled, it should be reported
// ASAP by filing a bug on github. Overriding non-buggy code is strongly discouraged to avoid the
// problem of the bugs being found after the old code path has been removed.
constexpr const char* runtime_features[] = {
    // Enabled
    "envoy.reloadable_features.test_feature_true",
    "envoy.reloadable_features.http2_protocol_options.max_outbound_control_frames",
    "envoy.reloadable_features.http2_protocol_options.stream_error_on_invalid_http_messaging",
    "envoy.reloadable_features.http2_protocol_options.",
    "envoy.reloadable_features.strict_header_validation",
    "envoy.reloadable_features.http2_protocol_options.max_inbound_priority_frames_per_stream",
    "envoy.reloadable_features.trusted_forwarded_proto",
    "envoy.reloadable_features.outlier_detection_support_for_grpc_status",
    "envoy.reloadable_features.buffer_filter_populate_content_length",
    "envoy.reloadable_features.http2_protocol_options.max_outbound_frames",
    "envoy.reloadable_features.buffer_filter_populate_content_length",
    "envoy.reloadable_features.trusted_forwarded_proto",
    "envoy.reloadable_features.outlier_detection_support_for_grpc_status",
};

// This is a list of configuration fields which are disallowed by default in Envoy
//
// By default, use of proto fields marked as deprecated in their api/.../*.proto file will result
// in a logged warning, so that Envoy users have a warning that they are using deprecated fields.
//
// During the Envoy release cycle, the maintainer team runs a script which will upgrade currently
// deprecated features to be disallowed (adding them to the list below) at which point use of said
// feature will cause a hard-failure (ProtoValidationException) instead of a logged warning.
//
// The release cycle after a feature has been marked disallowed, it is officially removable, and
// the maintainer team will run a script creating a tracking issue for proto and code clean up.
constexpr const char* disallowed_features[] = {
    // Acts as both a test entry for deprecated.proto and a marker for the Envoy
    // deprecation scripts.
    "envoy.deprecated_features.deprecated.proto:is_deprecated_fatal",
    "envoy.deprecated_features.route.proto:regex_match",
    "envoy.deprecated_features.route.proto:regex",
    "envoy.deprecated_features.accesslog.proto:config",
    "envoy.deprecated_features.thrift_proxy.proto:config",
    "envoy.deprecated_features.cds.proto:tls_context",
    "envoy.deprecated_features.route.proto:allow_origin",
    "envoy.deprecated_features.string.proto:regex",
    "envoy.deprecated_features.grpc_service.proto:config",
    "envoy.deprecated_features.tcp_proxy.proto:deprecated",
    "envoy.deprecated_features.overload.proto:config",
    "envoy.deprecated_features.route.proto:value",
    "envoy.deprecated_features.base.proto:config",
    "envoy.deprecated_features.http_connection_manager.proto:idle_timeout",
    "envoy.deprecated_features.lds.proto:use_original_dst",
    "envoy.deprecated_features.config_source.proto:DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE",
    "envoy.deprecated_features.cds.proto:config",
    "envoy.deprecated_features.route.proto:pattern",
    "envoy.deprecated_features.listener.proto:tls_context",
    "envoy.deprecated_features.listener.proto:config",
    "envoy.deprecated_features.health_check.proto:config",
    "envoy.deprecated_features.http_connection_manager.proto:[(validate.rules).enum",
    "envoy.deprecated_features.health_check.proto:use_http2",
    "envoy.deprecated_features.trace.proto:DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE",
    "envoy.deprecated_features.trace.proto:HTTP_JSON_V1",
    "envoy.deprecated_features.route.proto:config",
    "envoy.deprecated_features.cds.proto:ORIGINAL_DST_LB",
    "envoy.deprecated_features.http_connection_manager.proto:config",
    "envoy.deprecated_features.route.proto:[(validate.rules).repeated",
    "envoy.deprecated_features.stats.proto:config",
    "envoy.deprecated_features.trace.proto:config",
    "envoy.deprecated_features.tcp_proxy.proto:deprecated_v1",
    "envoy.deprecated_features.cds.proto:extension_protocol_options",
    "envoy.deprecated_features.route.proto:per_filter_config",
    "envoy.deprecated_features.route.proto:method",
    // 1.10.0
    "envoy.deprecated_features.config_source.proto:UNSUPPORTED_REST_LEGACY",
    "envoy.deprecated_features.ext_authz.proto:use_alpha",
    "envoy.deprecated_features.fault.proto:type",
    "envoy.deprecated_features.route.proto:enabled",
    "envoy.deprecated_features.route.proto:runtime_key",
    // 1.11.0
    "envoy.deprecated_features.bootstrap.proto:runtime",
    "envoy.deprecated_features.redis_proxy.proto:catch_all_cluster",
    "envoy.deprecated_features.redis_proxy.proto:cluster",
    "envoy.deprecated_features.server_info.proto:max_obj_name_len",
    "envoy.deprecated_features.server_info.proto:max_stats",
    "envoy.deprecated_features.v1_filter_json_config",
};

RuntimeFeatures::RuntimeFeatures() {
  for (auto& feature : disallowed_features) {
    disallowed_features_.insert(feature);
  }
  for (auto& feature : runtime_features) {
    enabled_features_.insert(feature);
  }
}

} // namespace Runtime
} // namespace Envoy
