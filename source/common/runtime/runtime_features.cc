#include "common/runtime/runtime_features.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Runtime {

bool isRuntimeFeature(absl::string_view feature) {
  return RuntimeFeaturesDefaults::get().enabledByDefault(feature) ||
         RuntimeFeaturesDefaults::get().existsButDisabled(feature);
}

bool runtimeFeatureEnabled(absl::string_view feature) {
  ASSERT(isRuntimeFeature(feature));
  if (Runtime::LoaderSingleton::getExisting()) {
    return Runtime::LoaderSingleton::getExisting()->threadsafeSnapshot()->runtimeFeatureEnabled(
        feature);
  }
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::runtime), warn,
                      "Unable to use runtime singleton for feature {}", feature);
  return RuntimeFeaturesDefaults::get().enabledByDefault(feature);
}

uint64_t getInteger(absl::string_view feature, uint64_t default_value) {
  ASSERT(absl::StartsWith(feature, "envoy."));
  if (Runtime::LoaderSingleton::getExisting()) {
    return Runtime::LoaderSingleton::getExisting()->threadsafeSnapshot()->getInteger(
        std::string(feature), default_value);
  }
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::runtime), warn,
                      "Unable to use runtime singleton for feature {}", feature);
  return default_value;
}

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
    // Begin alphabetically sorted section.
    "envoy.deprecated_features.allow_deprecated_extension_names",
    "envoy.reloadable_features.always_apply_route_header_rules",
    "envoy.reloadable_features.activate_fds_next_event_loop",
    "envoy.reloadable_features.activate_timers_next_event_loop",
    "envoy.reloadable_features.allow_500_after_100",
    "envoy.reloadable_features.allow_prefetch",
    "envoy.reloadable_features.allow_response_for_timeout",
    "envoy.reloadable_features.consume_all_retry_headers",
    "envoy.reloadable_features.check_ocsp_policy",
    "envoy.reloadable_features.disallow_unbounded_access_logs",
    "envoy.reloadable_features.early_errors_via_hcm",
    "envoy.reloadable_features.enable_deprecated_v2_api_warning",
    "envoy.reloadable_features.enable_dns_cache_circuit_breakers",
    "envoy.reloadable_features.fix_upgrade_response",
    "envoy.reloadable_features.fix_wildcard_matching",
    "envoy.reloadable_features.fixed_connection_close",
    "envoy.reloadable_features.hcm_stream_error_on_invalid_message",
    "envoy.reloadable_features.http_default_alpn",
    "envoy.reloadable_features.http_match_on_all_headers",
    "envoy.reloadable_features.http_set_copy_replace_all_headers",
    "envoy.reloadable_features.http_transport_failure_reason_in_body",
    "envoy.reloadable_features.http2_skip_encoding_empty_trailers",
    "envoy.reloadable_features.listener_in_place_filterchain_update",
    "envoy.reloadable_features.new_codec_behavior",
    "envoy.reloadable_features.overload_manager_disable_keepalive_drain_http2",
    "envoy.reloadable_features.prefer_quic_kernel_bpf_packet_routing",
    "envoy.reloadable_features.preserve_query_string_in_path_redirects",
    "envoy.reloadable_features.preserve_upstream_date",
    "envoy.reloadable_features.require_ocsp_response_for_must_staple_certs",
    "envoy.reloadable_features.stop_faking_paths",
    "envoy.reloadable_features.strict_1xx_and_204_response_headers",
    "envoy.reloadable_features.tls_use_io_handle_bio",
    "envoy.reloadable_features.unify_grpc_handling",
    "envoy.restart_features.use_apple_api_for_dns_lookups",
};

// This is a section for officially sanctioned runtime features which are too
// high risk to be enabled by default. Examples where we have opted to land
// features without enabling by default are swapping the underlying buffer
// implementation or the HTTP/1.1 codec implementation. Most features should be
// enabled by default.
//
// When features are added here, there should be a tracking bug assigned to the
// code owner to flip the default after sufficient testing.
constexpr const char* disabled_runtime_features[] = {
    // Allow Envoy to upgrade or downgrade version of type url, should be removed when support for
    // v2 url is removed from codebase.
    "envoy.reloadable_features.enable_type_url_downgrade_and_upgrade",
    // TODO(alyssawilk) flip true after the release.
    "envoy.reloadable_features.new_tcp_connection_pool",
    // TODO(yanavlasov) flip true after all tests for upstream flood checks are implemented
    "envoy.reloadable_features.upstream_http2_flood_checks",
    // Sentinel and test flag.
    "envoy.reloadable_features.test_feature_false",
    // gRPC Timeout header is missing (#13580)
    "envoy.reloadable_features.ext_authz_measure_timeout_on_check_created",
    // The cluster which can't extract secret entity by SDS to be warming and never activate.
    "envoy.reloadable_features.cluster_keep_warming_no_secret_entity",
};

RuntimeFeatures::RuntimeFeatures() {
  for (auto& feature : runtime_features) {
    enabled_features_.insert(feature);
  }
  for (auto& feature : disabled_runtime_features) {
    disabled_features_.insert(feature);
  }
}

} // namespace Runtime
} // namespace Envoy
