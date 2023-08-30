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
RUNTIME_GUARD(envoy_reloadable_features_allow_absolute_url_with_mixed_scheme);
RUNTIME_GUARD(envoy_reloadable_features_allow_compact_maglev);
RUNTIME_GUARD(envoy_reloadable_features_append_query_parameters_path_rewriter);
RUNTIME_GUARD(envoy_reloadable_features_append_xfh_idempotent);
RUNTIME_GUARD(envoy_reloadable_features_check_mep_on_first_eject);
RUNTIME_GUARD(envoy_reloadable_features_conn_pool_delete_when_idle);
RUNTIME_GUARD(envoy_reloadable_features_copy_response_code_to_downstream_stream_info);
RUNTIME_GUARD(envoy_reloadable_features_count_unused_mapped_pages_as_free);
RUNTIME_GUARD(envoy_reloadable_features_dfp_mixed_scheme);
RUNTIME_GUARD(envoy_reloadable_features_enable_aws_credentials_file);
RUNTIME_GUARD(envoy_reloadable_features_enable_compression_bomb_protection);
RUNTIME_GUARD(envoy_reloadable_features_enable_connect_udp_support);
RUNTIME_GUARD(envoy_reloadable_features_enable_intermediate_ca);
RUNTIME_GUARD(envoy_reloadable_features_enable_update_listener_socket_options);
RUNTIME_GUARD(envoy_reloadable_features_expand_agnostic_stream_lifetime);
RUNTIME_GUARD(envoy_reloadable_features_format_ports_as_numbers);
RUNTIME_GUARD(envoy_reloadable_features_handle_uppercase_scheme);
RUNTIME_GUARD(envoy_reloadable_features_http1_allow_codec_error_response_after_1xx_headers);
RUNTIME_GUARD(envoy_reloadable_features_http2_decode_metadata_with_quiche);
RUNTIME_GUARD(envoy_reloadable_features_http2_validate_authority_with_quiche);
RUNTIME_GUARD(envoy_reloadable_features_http_allow_partial_urls_in_referer);
RUNTIME_GUARD(envoy_reloadable_features_http_ext_auth_failure_mode_allow_header_add);
RUNTIME_GUARD(envoy_reloadable_features_http_filter_avoid_reentrant_local_reply);
// Delay deprecation and decommission until UHV is enabled.
RUNTIME_GUARD(envoy_reloadable_features_http_reject_path_with_fragment);
RUNTIME_GUARD(envoy_reloadable_features_ignore_optional_option_from_hcm_for_route_config);
RUNTIME_GUARD(envoy_reloadable_features_immediate_response_use_filter_mutation_rule);
RUNTIME_GUARD(envoy_reloadable_features_initialize_upstream_filters);
RUNTIME_GUARD(envoy_reloadable_features_keep_endpoint_active_hc_status_on_locality_update);
RUNTIME_GUARD(envoy_reloadable_features_lowercase_scheme);
RUNTIME_GUARD(envoy_reloadable_features_no_extension_lookup_by_name);
RUNTIME_GUARD(envoy_reloadable_features_no_full_scan_certs_on_sni_mismatch);
RUNTIME_GUARD(envoy_reloadable_features_oauth_header_passthrough_fix);
RUNTIME_GUARD(envoy_reloadable_features_oauth_make_token_cookie_httponly);
RUNTIME_GUARD(envoy_reloadable_features_oauth_use_standard_max_age_value);
RUNTIME_GUARD(envoy_reloadable_features_oauth_use_url_encoding);
RUNTIME_GUARD(envoy_reloadable_features_original_dst_rely_on_idle_timeout);
RUNTIME_GUARD(envoy_reloadable_features_overload_manager_error_unknown_action);
RUNTIME_GUARD(envoy_reloadable_features_prohibit_route_refresh_after_response_headers_sent);
RUNTIME_GUARD(envoy_reloadable_features_quic_defer_logging_to_ack_listener);
RUNTIME_GUARD(envoy_reloadable_features_sanitize_original_path);
RUNTIME_GUARD(envoy_reloadable_features_send_header_raw_value);
RUNTIME_GUARD(envoy_reloadable_features_service_sanitize_non_utf8_strings);
RUNTIME_GUARD(envoy_reloadable_features_skip_dns_lookup_for_proxied_requests);
RUNTIME_GUARD(envoy_reloadable_features_stateful_session_encode_ttl_in_cookie);
RUNTIME_GUARD(envoy_reloadable_features_successful_active_health_check_uneject_host);
RUNTIME_GUARD(envoy_reloadable_features_test_feature_true);
RUNTIME_GUARD(envoy_reloadable_features_thrift_allow_negative_field_ids);
RUNTIME_GUARD(envoy_reloadable_features_thrift_connection_draining);
RUNTIME_GUARD(envoy_reloadable_features_uhv_allow_malformed_url_encoding);
RUNTIME_GUARD(envoy_reloadable_features_upstream_allow_connect_with_2xx);
RUNTIME_GUARD(envoy_reloadable_features_upstream_wait_for_response_headers_before_disabling_read);
RUNTIME_GUARD(envoy_reloadable_features_use_http3_header_normalisation);
RUNTIME_GUARD(envoy_reloadable_features_validate_connect);
RUNTIME_GUARD(envoy_reloadable_features_validate_detailed_override_host_statuses);
RUNTIME_GUARD(envoy_reloadable_features_validate_grpc_header_before_log_grpc_status);
RUNTIME_GUARD(envoy_reloadable_features_validate_upstream_headers);
RUNTIME_GUARD(envoy_restart_features_explicit_wildcard_resource);
RUNTIME_GUARD(envoy_restart_features_udp_read_normalize_addresses);

// Begin false flags. These should come with a TODO to flip true.
// Sentinel and test flag.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_test_feature_false);
// TODO(paul-r-gall) Make this enabled by default after additional soak time.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_streaming_shadow);
// TODO(adisuissa) reset to true to enable unified mux by default
FALSE_RUNTIME_GUARD(envoy_reloadable_features_unified_mux);
// TODO(kbaichoo): Make this enabled by default when fairness and chunking
// are implemented, and we've had more cpu time.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_defer_processing_backedup_streams);
// TODO(birenroy) flip after a burn-in period
FALSE_RUNTIME_GUARD(envoy_reloadable_features_http2_use_oghttp2);
// TODO(bencebeky): Flip true after sufficient canarying.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_http1_use_balsa_parser);
// Used to track if runtime is initialized.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_runtime_initialized);
// TODO(mattklein123): Flip this to true and/or remove completely once verified by Envoy Mobile.
// TODO(mattklein123): Also unit test this if this sticks and this becomes the default for Apple &
// Android.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_always_use_v6);
// TODO(pradeepcrao) reset this to true after 2 releases (1.27)
FALSE_RUNTIME_GUARD(envoy_reloadable_features_enable_include_histograms);
// TODO(wbpcode) complete remove this feature is no one use it.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_refresh_rtt_after_request);
// TODO(danzh) false deprecate it once QUICHE has its own enable/disable flag.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_quic_reject_all);
// TODO(adisuissa): enable by default once this is tested in prod.
FALSE_RUNTIME_GUARD(envoy_restart_features_use_eds_cache_for_ads);

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
