#include "source/common/runtime/runtime_features.h"

#include "envoy/http/codec_runtime_overrides.h"

#include "source/common/singleton/const_singleton.h"

#include "absl/flags/commandlineflag.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"

#define RUNTIME_GUARD(name) ABSL_FLAG(bool, name, true, "");        // NOLINT
#define FALSE_RUNTIME_GUARD(name) ABSL_FLAG(bool, name, false, ""); // NOLINT

// Add additional features here to enable the new code paths by default.
//
// Per documentation in CONTRIBUTING.md is expected that new high risk code paths be guarded
// by runtime feature guards. If you add a guard of the form
// RUNTIME_GUARD(envoy_reloadable_features_my_feature_name)
// here you can guard code checking against "envoy.reloadable_features.my_feature_name".
// Please note the swap of envoy_reloadable_features_ to envoy.reloadable_features.!
//
// if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.my_feature_name")) {
//   [new code path]
// else {
//   [old_code_path]
// }
//
// Runtime features are true by default, so the new code path is exercised.
// To make a runtime feature false by default, use FALSE_RUNTIME_GUARD, and add
// a TODO to change it to true.
//
// If issues are found that require a runtime feature to be disabled, it should be reported
// ASAP by filing a bug on github. Overriding non-buggy code is strongly discouraged to avoid the
// problem of the bugs being found after the old code path has been removed.
RUNTIME_GUARD(envoy_reloadable_features_async_host_selection);
RUNTIME_GUARD(envoy_reloadable_features_decouple_explicit_drain_pools_and_dns_refresh);
RUNTIME_GUARD(envoy_reloadable_features_dfp_cluster_resolves_hosts);
RUNTIME_GUARD(envoy_reloadable_features_disallow_quic_client_udp_mmsg);
RUNTIME_GUARD(envoy_reloadable_features_enable_cel_regex_precompilation);
RUNTIME_GUARD(envoy_reloadable_features_enable_compression_bomb_protection);
RUNTIME_GUARD(envoy_reloadable_features_enable_new_query_param_present_match_behavior);
// Ignore the automated "remove this flag" issue: we should keep this for 1 year. Confirm with
// @yanjunxiang-google before removing.
RUNTIME_GUARD(envoy_reloadable_features_ext_proc_fail_close_spurious_resp);
RUNTIME_GUARD(envoy_reloadable_features_ext_proc_stream_close_optimization);
RUNTIME_GUARD(envoy_reloadable_features_generic_proxy_codec_buffer_limit);
RUNTIME_GUARD(envoy_reloadable_features_grpc_side_stream_flow_control);
RUNTIME_GUARD(envoy_reloadable_features_http1_balsa_delay_reset);
RUNTIME_GUARD(envoy_reloadable_features_http1_balsa_disallow_lone_cr_in_chunk_extension);
RUNTIME_GUARD(envoy_reloadable_features_http2_discard_host_header);
RUNTIME_GUARD(envoy_reloadable_features_http2_propagate_reset_events);
RUNTIME_GUARD(envoy_reloadable_features_http2_use_oghttp2);
// Delay deprecation and decommission until UHV is enabled.
RUNTIME_GUARD(envoy_reloadable_features_http_reject_path_with_fragment);
RUNTIME_GUARD(envoy_reloadable_features_mobile_use_network_observer_registry);
RUNTIME_GUARD(envoy_reloadable_features_no_extension_lookup_by_name);
RUNTIME_GUARD(envoy_reloadable_features_oauth2_cleanup_cookies);
RUNTIME_GUARD(envoy_reloadable_features_oauth2_encrypt_tokens);
RUNTIME_GUARD(envoy_reloadable_features_odcds_over_ads_fix);
RUNTIME_GUARD(envoy_reloadable_features_original_dst_rely_on_idle_timeout);
RUNTIME_GUARD(envoy_reloadable_features_prefix_map_matcher_resume_after_subtree_miss);
RUNTIME_GUARD(envoy_reloadable_features_quic_defer_logging_to_ack_listener);
RUNTIME_GUARD(envoy_reloadable_features_quic_fix_defer_logging_miss_for_half_closed_stream);
// Ignore the automated "remove this flag" issue: we should keep this for 1 year. Confirm with
// @danzh2010 or @RyanTheOptimist before removing.
RUNTIME_GUARD(envoy_reloadable_features_quic_send_server_preferred_address_to_all_clients);
RUNTIME_GUARD(envoy_reloadable_features_quic_signal_headers_only_to_http1_backend);
RUNTIME_GUARD(envoy_reloadable_features_quic_upstream_reads_fixed_number_packets);
RUNTIME_GUARD(envoy_reloadable_features_quic_upstream_socket_use_address_cache_for_read);
RUNTIME_GUARD(envoy_reloadable_features_reject_empty_trusted_ca_file);
RUNTIME_GUARD(envoy_reloadable_features_report_load_when_rq_active_is_non_zero);
RUNTIME_GUARD(envoy_reloadable_features_router_filter_resetall_on_local_reply);
RUNTIME_GUARD(envoy_reloadable_features_safe_http2_options);
RUNTIME_GUARD(envoy_reloadable_features_skip_dns_lookup_for_proxied_requests);
RUNTIME_GUARD(envoy_reloadable_features_tcp_proxy_set_idle_timer_immediately_on_new_connection);
RUNTIME_GUARD(envoy_reloadable_features_test_feature_true);
RUNTIME_GUARD(envoy_reloadable_features_trace_refresh_after_route_refresh);
RUNTIME_GUARD(envoy_reloadable_features_udp_set_do_not_fragment);
RUNTIME_GUARD(envoy_reloadable_features_uhv_allow_malformed_url_encoding);
RUNTIME_GUARD(envoy_reloadable_features_uri_template_match_on_asterisk);
RUNTIME_GUARD(envoy_reloadable_features_use_migration_in_quiche);
RUNTIME_GUARD(envoy_reloadable_features_use_response_decoder_handle);
RUNTIME_GUARD(envoy_reloadable_features_validate_connect);
RUNTIME_GUARD(envoy_reloadable_features_validate_upstream_headers);
RUNTIME_GUARD(envoy_reloadable_features_websocket_allow_4xx_5xx_through_filter_chain);
RUNTIME_GUARD(envoy_reloadable_features_websocket_enable_timeout_on_upgrade_response);
RUNTIME_GUARD(envoy_reloadable_features_xds_failover_to_primary_enabled);

RUNTIME_GUARD(envoy_restart_features_move_locality_schedulers_to_lb);
RUNTIME_GUARD(envoy_restart_features_raise_file_limits);
RUNTIME_GUARD(envoy_restart_features_skip_backing_cluster_check_for_sds);
RUNTIME_GUARD(envoy_restart_features_use_eds_cache_for_ads);
RUNTIME_GUARD(envoy_restart_features_validate_http3_pseudo_headers);

// Begin false flags. Most of them should come with a TODO to flip true.

// Sentinel and test flag.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_test_feature_false);
// TODO(adisuissa) reset to true to enable unified mux by default
FALSE_RUNTIME_GUARD(envoy_reloadable_features_unified_mux);
// Used to track if runtime is initialized.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_runtime_initialized);
// TODO(alyssawilk, renjietang) figure out what to do with this for optimal defaults
#if defined(__ANDROID_API__)
ABSL_FLAG(bool, envoy_reloadable_features_always_use_v6, true, "");
#else
FALSE_RUNTIME_GUARD(envoy_reloadable_features_always_use_v6);
#endif
// TODO(vikaschoudhary16) flip this to true only after all the
// TcpProxy::Filter::HttpStreamDecoderFilterCallbacks are implemented or commented as unnecessary
FALSE_RUNTIME_GUARD(envoy_restart_features_upstream_http_filters_with_tcp_proxy);
// TODO(danzh) false deprecate it once QUICHE has its own enable/disable flag.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_quic_reject_all);
// TODO(#10646) change to true when UHV is sufficiently tested
// For more information about Universal Header Validation, please see
// https://github.com/envoyproxy/envoy/issues/10646
FALSE_RUNTIME_GUARD(envoy_reloadable_features_enable_universal_header_validator);
// TODO(alyssar) evaluate and either make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_reresolve_null_addresses);
// TODO(alyssar) evaluate and either make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_reresolve_if_no_connections);
// TODO(adisuissa): flip to true after this is out of alpha mode.
FALSE_RUNTIME_GUARD(envoy_restart_features_xds_failover_support);
// TODO(abeyad): evaluate and either make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_dns_cache_set_ip_version_to_remove);
// TODO(abeyad): evaluate and either make this the default or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_dns_cache_filter_unusable_ip_version);
// TODO(abeyad): evaluate and make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_drain_pools_on_network_change);
// TODO(fredyw): evaluate and either make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_quic_no_tcp_delay);
// Adding runtime flag to use balsa_parser for http_inspector.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_http_inspector_use_balsa_parser);
// TODO(danzh) re-enable it when the issue of preferring TCP over v6 rather than QUIC over v4 is
// fixed.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_http3_happy_eyeballs);
// TODO(renjietang): Evaluate and make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_use_canonical_suffix_for_quic_brokenness);
// TODO(abeyad): Evaluate and make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_use_canonical_suffix_for_srtt);
// TODO(abeyad): Evaluate and make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_use_canonical_suffix_for_initial_rtt_estimate);
// TODO(fredyw): Remove after done with debugging.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_log_ip_families_on_network_error);
// TODO(abeyad): Flip to true after prod testing. Simple filtering applies to link-local addresses
// only.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_mobile_ipv6_probe_simple_filtering);
// TODO(abeyad): Flip to true after prod testing. Advanced filtering applies to all IP reserved
// range addresses.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_mobile_ipv6_probe_advanced_filtering);
// TODO(botengyao): flip to true after canarying the feature internally without problems.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_connection_close_through_filter_manager);
// TODO(adisuissa): flip to true after all xDS types use the new subscription
// method, and this is tested extensively.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_xdstp_based_config_singleton_subscriptions);

// A flag to set the maximum TLS version for google_grpc client to TLS1.2, when needed for
// compliance restrictions.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_google_grpc_disable_tls_13);

// TODO(yanavlasov): Flip to true after prod testing.
// Controls whether a stream stays open when HTTP/2 or HTTP/3 upstream half closes
// before downstream.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_allow_multiplexed_upstream_half_close);

FALSE_RUNTIME_GUARD(envoy_reloadable_features_ext_proc_graceful_grpc_close);

FALSE_RUNTIME_GUARD(envoy_reloadable_features_getaddrinfo_no_ai_flags);

// Flag to remove legacy route formatter support in header parser
// Flip to true after two release periods.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_remove_legacy_route_formatter);

// TODO(grnmeira):
// Enables the new DNS implementation, a merged implementation of
// strict and logical DNS clusters. This new implementation will
// take over the split ones, and will be used as a base for the
// implementation of on-demand DNS.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_enable_new_dns_implementation);
// Force a local reply from upstream envoy for reverse connections.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_reverse_conn_force_local_reply);
// RELEASE_ASSERT when upstream stream detects UAF of downstream response decoder instance.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_abort_when_accessing_dead_decoder);
// TODO(pradeepcrao): Create a config option to enable this instead after
// testing.
FALSE_RUNTIME_GUARD(envoy_restart_features_use_cached_grpc_client_for_xds);
// Runtime guard to revert back to old non-RFC-compliant CONNECT behavior without Host header.
// TODO(vinaykul): Drop this false-runtime-guard when deemed safe with RFC 9110 compliant CONNECT.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_http_11_proxy_connect_legacy_format);
// TODO(tsaarni): Flip to true after prod testing or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_fixed_heap_use_allocated);

// Block of non-boolean flags. Use of int flags is deprecated. Do not add more.
ABSL_FLAG(uint64_t, re2_max_program_size_error_level, 100, ""); // NOLINT
ABSL_FLAG(uint64_t, re2_max_program_size_warn_level,            // NOLINT
          std::numeric_limits<uint32_t>::max(), "");            // NOLINT

namespace Envoy {
namespace Runtime {
namespace {

std::string swapPrefix(std::string name) {
  return absl::StrReplaceAll(name, {{"envoy_", "envoy."}, {"features_", "features."}});
}

} // namespace

// This is a singleton class to map Envoy style flag names to absl flags
class RuntimeFeatures {
public:
  RuntimeFeatures();

  // Get the command line flag corresponding to the Envoy style feature name, or
  // nullptr if it is not a registered flag.
  absl::CommandLineFlag* getFlag(absl::string_view feature) const {
    auto it = all_features_.find(feature);
    if (it == all_features_.end()) {
      return nullptr;
    }
    return it->second;
  }

private:
  absl::flat_hash_map<std::string, absl::CommandLineFlag*> all_features_;
};

using RuntimeFeaturesDefaults = ConstSingleton<RuntimeFeatures>;

RuntimeFeatures::RuntimeFeatures() {
  absl::flat_hash_map<absl::string_view, absl::CommandLineFlag*> flags = absl::GetAllFlags();
  for (auto& it : flags) {
    absl::string_view name = it.second->Name();
    if ((!absl::StartsWith(name, "envoy_reloadable_features_") &&
         !absl::StartsWith(name, "envoy_restart_features_")) ||
        !it.second->TryGet<bool>().has_value()) {
      continue;
    }
    std::string envoy_name = swapPrefix(std::string(name));
    all_features_.emplace(envoy_name, it.second);
  }
}

bool hasRuntimePrefix(absl::string_view feature) {
  // Track Envoy reloadable and restart features, excluding synthetic QUIC flags
  // which are not tracked in the list below.
  return (absl::StartsWith(feature, "envoy.reloadable_features.") &&
          !absl::StartsWith(feature, "envoy.reloadable_features.FLAGS_envoy_quic")) ||
         absl::StartsWith(feature, "envoy.restart_features.");
}

bool isRuntimeFeature(absl::string_view feature) {
  return RuntimeFeaturesDefaults::get().getFlag(feature) != nullptr;
}

bool isLegacyRuntimeFeature(absl::string_view feature) {
  return feature == Http::MaxRequestHeadersCountOverrideKey ||
         feature == Http::MaxResponseHeadersCountOverrideKey ||
         feature == Http::MaxRequestHeadersSizeOverrideKey ||
         feature == Http::MaxResponseHeadersSizeOverrideKey;
}

bool runtimeFeatureEnabled(absl::string_view feature) {
  absl::CommandLineFlag* flag = RuntimeFeaturesDefaults::get().getFlag(feature);
  if (flag == nullptr) {
    IS_ENVOY_BUG(absl::StrCat("Unable to find runtime feature ", feature));
    return false;
  }
  // We validate in map creation that the flag is a boolean.
  return flag->TryGet<bool>().value();
}

uint64_t getInteger(absl::string_view feature, uint64_t default_value) {
  // DO NOT ADD MORE FLAGS HERE. This function deprecated.
  if (absl::StartsWith(feature, "re2.")) {
    if (feature == "re2.max_program_size.error_level") {
      return absl::GetFlag(FLAGS_re2_max_program_size_error_level);
    } else if (feature == "re2.max_program_size.warn_level") {
      return absl::GetFlag(FLAGS_re2_max_program_size_warn_level);
    }
  }
  IS_ENVOY_BUG(absl::StrCat("requested an unsupported integer ", feature));
  return default_value;
}

void markRuntimeInitialized() {
  maybeSetRuntimeGuard("envoy.reloadable_features.runtime_initialized", true);
}

bool isRuntimeInitialized() {
  return runtimeFeatureEnabled("envoy.reloadable_features.runtime_initialized");
}

void maybeSetRuntimeGuard(absl::string_view name, bool value) {
  absl::CommandLineFlag* flag = RuntimeFeaturesDefaults::get().getFlag(name);
  if (flag == nullptr) {
    IS_ENVOY_BUG(absl::StrCat("Unable to find runtime feature ", name));
    return;
  }
  std::string err;
  flag->ParseFrom(value ? "true" : "false", &err);
}

void maybeSetDeprecatedInts(absl::string_view name, uint32_t value) {
  if (!absl::StartsWith(name, "envoy.") && !absl::StartsWith(name, "re2.")) {
    return;
  }

  // DO NOT ADD MORE FLAGS HERE. This function deprecated.
  else if (name == "re2.max_program_size.error_level") {
    absl::SetFlag(&FLAGS_re2_max_program_size_error_level, value);
  } else if (name == "re2.max_program_size.warn_level") {
    absl::SetFlag(&FLAGS_re2_max_program_size_warn_level, value);
  }
}

} // namespace Runtime
} // namespace Envoy
