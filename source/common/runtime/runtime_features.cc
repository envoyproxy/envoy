#include "source/common/runtime/runtime_features.h"

#include "absl/flags/commandlineflag.h"
#include "absl/flags/flag.h"
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
RUNTIME_GUARD(envoy_reloadable_features_abort_filter_chain_on_stream_reset);
RUNTIME_GUARD(envoy_reloadable_features_avoid_zombie_streams);
RUNTIME_GUARD(envoy_reloadable_features_check_mep_on_first_eject);
RUNTIME_GUARD(envoy_reloadable_features_check_switch_protocol_websocket_handshake);
RUNTIME_GUARD(envoy_reloadable_features_conn_pool_delete_when_idle);
RUNTIME_GUARD(envoy_reloadable_features_consistent_header_validation);
RUNTIME_GUARD(envoy_reloadable_features_defer_processing_backedup_streams);
RUNTIME_GUARD(envoy_reloadable_features_dfp_mixed_scheme);
RUNTIME_GUARD(envoy_reloadable_features_disallow_quic_client_udp_mmsg);
RUNTIME_GUARD(envoy_reloadable_features_dns_cache_set_first_resolve_complete);
RUNTIME_GUARD(envoy_reloadable_features_dns_nodata_noname_is_success);
RUNTIME_GUARD(envoy_reloadable_features_dns_reresolve_on_eai_again);
RUNTIME_GUARD(envoy_reloadable_features_edf_lb_host_scheduler_init_fix);
RUNTIME_GUARD(envoy_reloadable_features_edf_lb_locality_scheduler_init_fix);
RUNTIME_GUARD(envoy_reloadable_features_enable_compression_bomb_protection);
RUNTIME_GUARD(envoy_reloadable_features_enable_connect_udp_support);
RUNTIME_GUARD(envoy_reloadable_features_enable_include_histograms);
RUNTIME_GUARD(envoy_reloadable_features_enable_intermediate_ca);
RUNTIME_GUARD(envoy_reloadable_features_enable_zone_routing_different_zone_counts);
RUNTIME_GUARD(envoy_reloadable_features_exclude_host_in_eds_status_draining);
RUNTIME_GUARD(envoy_reloadable_features_ext_authz_http_send_original_xff);
RUNTIME_GUARD(envoy_reloadable_features_grpc_http1_reverse_bridge_change_http_status);
RUNTIME_GUARD(envoy_reloadable_features_grpc_http1_reverse_bridge_handle_empty_response);
RUNTIME_GUARD(envoy_reloadable_features_hmac_base64_encoding_only);
RUNTIME_GUARD(envoy_reloadable_features_http1_balsa_delay_reset);
RUNTIME_GUARD(envoy_reloadable_features_http1_connection_close_header_in_redirect);
// Ignore the automated "remove this flag" issue: we should keep this for 1 year.
RUNTIME_GUARD(envoy_reloadable_features_http1_use_balsa_parser);
RUNTIME_GUARD(envoy_reloadable_features_http2_decode_metadata_with_quiche);
RUNTIME_GUARD(envoy_reloadable_features_http2_discard_host_header);
// Ignore the automated "remove this flag" issue: we should keep this for 1 year.
RUNTIME_GUARD(envoy_reloadable_features_http2_use_oghttp2);
RUNTIME_GUARD(envoy_reloadable_features_http2_use_visitor_for_data);
RUNTIME_GUARD(envoy_reloadable_features_http2_validate_authority_with_quiche);
RUNTIME_GUARD(envoy_reloadable_features_http_allow_partial_urls_in_referer);
RUNTIME_GUARD(envoy_reloadable_features_http_filter_avoid_reentrant_local_reply);
// Delay deprecation and decommission until UHV is enabled.
RUNTIME_GUARD(envoy_reloadable_features_http_reject_path_with_fragment);
RUNTIME_GUARD(envoy_reloadable_features_http_route_connect_proxy_by_default);
RUNTIME_GUARD(envoy_reloadable_features_immediate_response_use_filter_mutation_rule);
RUNTIME_GUARD(envoy_reloadable_features_locality_routing_use_new_routing_logic);
RUNTIME_GUARD(envoy_reloadable_features_no_downgrade_to_canonical_name);
RUNTIME_GUARD(envoy_reloadable_features_no_extension_lookup_by_name);
RUNTIME_GUARD(envoy_reloadable_features_no_full_scan_certs_on_sni_mismatch);
RUNTIME_GUARD(envoy_reloadable_features_normalize_host_for_preresolve_dfp_dns);
RUNTIME_GUARD(envoy_reloadable_features_oauth_make_token_cookie_httponly);
RUNTIME_GUARD(envoy_reloadable_features_oauth_use_standard_max_age_value);
RUNTIME_GUARD(envoy_reloadable_features_oauth_use_url_encoding);
RUNTIME_GUARD(envoy_reloadable_features_original_dst_rely_on_idle_timeout);
RUNTIME_GUARD(envoy_reloadable_features_proxy_status_mapping_more_core_response_flags);
RUNTIME_GUARD(envoy_reloadable_features_quic_fix_filter_manager_uaf);
RUNTIME_GUARD(envoy_reloadable_features_quic_receive_ecn);
// Ignore the automated "remove this flag" issue: we should keep this for 1 year. Confirm with
// @danzh2010 or @RyanTheOptimist before removing.
RUNTIME_GUARD(envoy_reloadable_features_quic_send_server_preferred_address_to_all_clients);
RUNTIME_GUARD(envoy_reloadable_features_quic_upstream_reads_fixed_number_packets);
RUNTIME_GUARD(envoy_reloadable_features_sanitize_te);
RUNTIME_GUARD(envoy_reloadable_features_send_header_raw_value);
RUNTIME_GUARD(envoy_reloadable_features_send_local_reply_when_no_buffer_and_upstream_request);
RUNTIME_GUARD(envoy_reloadable_features_skip_dns_lookup_for_proxied_requests);
RUNTIME_GUARD(envoy_reloadable_features_ssl_transport_failure_reason_format);
RUNTIME_GUARD(envoy_reloadable_features_stateful_session_encode_ttl_in_cookie);
RUNTIME_GUARD(envoy_reloadable_features_stop_decode_metadata_on_local_reply);
RUNTIME_GUARD(envoy_reloadable_features_strict_duration_validation);
RUNTIME_GUARD(envoy_reloadable_features_tcp_tunneling_send_downstream_fin_on_upstream_trailers);
RUNTIME_GUARD(envoy_reloadable_features_test_feature_true);
RUNTIME_GUARD(envoy_reloadable_features_thrift_allow_negative_field_ids);
RUNTIME_GUARD(envoy_reloadable_features_udp_socket_apply_aggregated_read_limit);
RUNTIME_GUARD(envoy_reloadable_features_uhv_allow_malformed_url_encoding);
RUNTIME_GUARD(envoy_reloadable_features_upstream_allow_connect_with_2xx);
RUNTIME_GUARD(envoy_reloadable_features_upstream_remote_address_use_connection);
RUNTIME_GUARD(envoy_reloadable_features_upstream_wait_for_response_headers_before_disabling_read);
RUNTIME_GUARD(envoy_reloadable_features_use_http3_header_normalisation);
RUNTIME_GUARD(envoy_reloadable_features_use_typed_metadata_in_proxy_protocol_listener);
RUNTIME_GUARD(envoy_reloadable_features_validate_connect);
RUNTIME_GUARD(envoy_reloadable_features_validate_grpc_header_before_log_grpc_status);
RUNTIME_GUARD(envoy_reloadable_features_validate_upstream_headers);
RUNTIME_GUARD(envoy_reloadable_features_xdstp_path_avoid_colon_encoding);
RUNTIME_GUARD(envoy_restart_features_allow_client_socket_creation_failure);
RUNTIME_GUARD(envoy_restart_features_allow_slot_destroy_on_worker_threads);
RUNTIME_GUARD(envoy_restart_features_quic_handle_certs_with_shared_tls_code);
RUNTIME_GUARD(envoy_restart_features_send_goaway_for_premature_rst_streams);
RUNTIME_GUARD(envoy_restart_features_udp_read_normalize_addresses);
RUNTIME_GUARD(envoy_restart_features_use_fast_protobuf_hash);

// Begin false flags. Most of them should come with a TODO to flip true.

// Execution context is optional and must be enabled explicitly.
// See https://github.com/envoyproxy/envoy/issues/32012.
FALSE_RUNTIME_GUARD(envoy_restart_features_enable_execution_context);
// Sentinel and test flag.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_test_feature_false);
// TODO(paul-r-gall) Make this enabled by default after additional soak time.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_streaming_shadow);
// TODO(adisuissa) reset to true to enable unified mux by default
FALSE_RUNTIME_GUARD(envoy_reloadable_features_unified_mux);
// Used to track if runtime is initialized.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_runtime_initialized);
// TODO(mattklein123): Flip this to true and/or remove completely once verified by Envoy Mobile.
// TODO(mattklein123): Also unit test this if this sticks and this becomes the default for Apple &
// Android.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_always_use_v6);
// TODO(vikaschoudhary16) flip this to true only after all the
// TcpProxy::Filter::HttpStreamDecoderFilterCallbacks are implemented or commented as unnecessary
FALSE_RUNTIME_GUARD(envoy_restart_features_upstream_http_filters_with_tcp_proxy);
// TODO(danzh) false deprecate it once QUICHE has its own enable/disable flag.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_quic_reject_all);
// TODO(suniltheta): Once the newly added http async technique is stabilized move it under
// RUNTIME_GUARD so that this option becomes default enabled. Once this option proves effective
// remove the feature flag and remove code path that relies on old technique to fetch credentials
// via libcurl and remove the bazel steps to pull and test the curl dependency.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_use_http_client_to_fetch_aws_credentials);
// TODO(adisuissa): enable by default once this is tested in prod.
FALSE_RUNTIME_GUARD(envoy_restart_features_use_eds_cache_for_ads);
// TODO(#10646) change to true when UHV is sufficiently tested
// For more information about Universal Header Validation, please see
// https://github.com/envoyproxy/envoy/issues/10646
FALSE_RUNTIME_GUARD(envoy_reloadable_features_enable_universal_header_validator);
// TODO(pksohn): enable after canarying fix for https://github.com/envoyproxy/envoy/issues/29930
FALSE_RUNTIME_GUARD(envoy_reloadable_features_quic_defer_logging_to_ack_listener);
// TODO(panting): flip this to true after some test time.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_use_config_in_happy_eyeballs);
// TODO(#33474) removed it once GRO packet dropping is fixed.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_prefer_quic_client_udp_gro);
// TODO(alyssar) evaluate and either make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_reresolve_null_addresses);
// TODO(alyssar) evaluate and either make this a config knob or remove.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_reresolve_if_no_connections);
// TODO(adisuissa): flip to true after this is out of alpha mode.
FALSE_RUNTIME_GUARD(envoy_restart_features_xds_failover_support);

// A flag to set the maximum TLS version for google_grpc client to TLS1.2, when needed for
// compliance restrictions.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_google_grpc_disable_tls_13);

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
