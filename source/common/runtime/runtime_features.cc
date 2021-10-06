#include "source/common/runtime/runtime_features.h"

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
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::runtime), debug,
                      "Unable to use runtime singleton for feature {}", feature);
  return RuntimeFeaturesDefaults::get().enabledByDefault(feature);
}

uint64_t getInteger(absl::string_view feature, uint64_t default_value) {
  ASSERT(absl::StartsWith(feature, "envoy."));
  if (Runtime::LoaderSingleton::getExisting()) {
    return Runtime::LoaderSingleton::getExisting()->threadsafeSnapshot()->getInteger(
        std::string(feature), default_value);
  }
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::runtime), debug,
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
    "envoy.reloadable_features.FLAGS_quic_reloadable_flag_quic_single_ack_in_packet2",
    "envoy.reloadable_features.add_and_validate_scheme_header",
    "envoy.reloadable_features.allow_response_for_timeout",
    "envoy.reloadable_features.check_unsupported_typed_per_filter_config",
    "envoy.reloadable_features.conn_pool_delete_when_idle",
    "envoy.reloadable_features.correct_scheme_and_xfp",
    "envoy.reloadable_features.disable_tls_inspector_injection",
    "envoy.reloadable_features.fix_added_trailers",
    "envoy.reloadable_features.grpc_bridge_stats_disabled",
    "envoy.reloadable_features.grpc_web_fix_non_proto_encoded_response_handling",
    "envoy.reloadable_features.grpc_json_transcoder_adhere_to_buffer_limits",
    "envoy.reloadable_features.hash_multiple_header_values",
    "envoy.reloadable_features.health_check.graceful_goaway_handling",
    "envoy.reloadable_features.health_check.immediate_failure_exclude_from_cluster",
    "envoy.reloadable_features.http2_consume_stream_refused_errors",
    "envoy.reloadable_features.http2_skip_encoding_empty_trailers",
    "envoy.reloadable_features.http_ext_authz_do_not_skip_direct_response_and_redirect",
    "envoy.reloadable_features.http_reject_path_with_fragment",
    "envoy.reloadable_features.http_strip_fragment_from_path_unsafe_if_disabled",
    "envoy.reloadable_features.http_transport_failure_reason_in_body",
    "envoy.reloadable_features.improved_stream_limit_handling",
    "envoy.reloadable_features.internal_redirects_with_body",
    "envoy.reloadable_features.listener_reuse_port_default_enabled",
    "envoy.reloadable_features.listener_wildcard_match_ip_family",
    "envoy.reloadable_features.new_tcp_connection_pool",
    "envoy.reloadable_features.no_chunked_encoding_header_for_304",
    "envoy.reloadable_features.preserve_downstream_scheme",
    "envoy.reloadable_features.remove_forked_chromium_url",
    "envoy.reloadable_features.require_strict_1xx_and_204_response_headers",
    "envoy.reloadable_features.send_strict_1xx_and_204_response_headers",
    "envoy.reloadable_features.strip_port_from_connect",
    "envoy.reloadable_features.treat_host_like_authority",
    "envoy.reloadable_features.udp_listener_updates_filter_chain_in_place",
    "envoy.reloadable_features.udp_per_event_loop_read_limit",
    "envoy.reloadable_features.unquote_log_string_values",
    "envoy.reloadable_features.upstream_host_weight_change_causes_rebuild",
    "envoy.reloadable_features.use_observable_cluster_name",
    "envoy.reloadable_features.validate_connect",
    "envoy.reloadable_features.vhds_heartbeats",
    "envoy.reloadable_features.wasm_cluster_name_envoy_grpc",
    "envoy.reloadable_features.upstream_http2_flood_checks",
    "envoy.restart_features.use_apple_api_for_dns_lookups",
    "envoy.reloadable_features.header_map_correctly_coalesce_cookies",
    "envoy.reloadable_features.sanitize_http_header_referer",
    "envoy.reloadable_features.skip_dispatching_frames_for_closed_connection",
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
    // TODO(asraa) flip to true in a separate PR to enable the new JSON by default.
    "envoy.reloadable_features.remove_legacy_json",
    // Sentinel and test flag.
    "envoy.reloadable_features.test_feature_false",
    // Allows the use of ExtensionWithMatcher to wrap a HTTP filter with a match tree.
    "envoy.reloadable_features.experimental_matching_api",
    // When the runtime is flipped to true, use shared cache in getOrCreateRawAsyncClient method if
    // CacheOption is CacheWhenRuntimeEnabled.
    // Caller that use AlwaysCache option will always cache, unaffected by this runtime.
    "envoy.reloadable_features.enable_grpc_async_client_cache",
};

RuntimeFeatures::RuntimeFeatures() {
#ifdef ENVOY_ENABLE_QUIC
#define STRINGIFY(X) #X
#define QUIC_FLAG(flag, value)                                                                     \
  if (value) {                                                                                     \
    if (absl::StartsWith(#flag, "FLAGS_quic_reloadable_flag")) {                                   \
      enabled_features_.insert(STRINGIFY(envoy.reloadable_features.flag));                         \
    } else if (absl::StartsWith(#flag, "FLAGS_quic_restart_flag")) {                               \
      enabled_features_.insert(STRINGIFY(envoy.restart_features.flag));                            \
    }                                                                                              \
  } else {                                                                                         \
    if (absl::StartsWith(#flag, "FLAGS_quic_reloadable_flag")) {                                   \
      disabled_features_.insert(STRINGIFY(envoy.reloadable_features.flag));                        \
    } else if (absl::StartsWith(#flag, "FLAGS_quic_restart_flag")) {                               \
      disabled_features_.insert(STRINGIFY(envoy.restart_features.flag));                           \
    }                                                                                              \
  }
// Apply QUICHE upstream default values at first + some additional flags used in tests.
// These may be overridden by Envoy default settings below.
#include "quiche/quic/core/quic_flags_list.h"
  QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_true, true)
  QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_true, true)
  QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_true, true)
  QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_true, true)
#undef QUIC_FLAG
#endif

  for (auto& feature : runtime_features) {
    if (disabled_features_.contains(feature)) {
      disabled_features_.erase(feature);
    }
    enabled_features_.insert(feature);
  }
  for (auto& feature : disabled_runtime_features) {
    if (enabled_features_.contains(feature)) {
      enabled_features_.erase(feature);
    }
    disabled_features_.insert(feature);
  }
}

} // namespace Runtime
} // namespace Envoy
