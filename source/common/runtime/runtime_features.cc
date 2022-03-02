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
// by runtime feature guards, i.e
//
// If you add a guard of the form
// RUNTIME_GUARD(envoy_reloadable_features_my_feature_nae_name)
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
RUNTIME_GUARD(envoy_reloadable_features_allow_upstream_inline_write);
RUNTIME_GUARD(envoy_reloadable_features_append_or_truncate);
RUNTIME_GUARD(envoy_reloadable_features_conn_pool_delete_when_idle);
RUNTIME_GUARD(envoy_reloadable_features_conn_pool_new_stream_with_early_data_and_http3);
RUNTIME_GUARD(envoy_reloadable_features_correct_scheme_and_xfp);
RUNTIME_GUARD(envoy_reloadable_features_correctly_validate_alpn);
RUNTIME_GUARD(envoy_reloadable_features_disable_tls_inspector_injection);
RUNTIME_GUARD(envoy_reloadable_features_do_not_await_headers_on_upstream_timeout_to_emit_stats);
RUNTIME_GUARD(envoy_reloadable_features_enable_grpc_async_client_cache);
RUNTIME_GUARD(envoy_reloadable_features_fix_added_trailers);
RUNTIME_GUARD(envoy_reloadable_features_handle_stream_reset_during_hcm_encoding);
RUNTIME_GUARD(envoy_reloadable_features_http2_allow_capacity_increase_by_settings);
RUNTIME_GUARD(envoy_reloadable_features_http2_new_codec_wrapper);
RUNTIME_GUARD(envoy_reloadable_features_http_ext_authz_do_not_skip_direct_response_and_redirect);
RUNTIME_GUARD(envoy_reloadable_features_http_reject_path_with_fragment);
RUNTIME_GUARD(envoy_reloadable_features_http_strip_fragment_from_path_unsafe_if_disabled);
RUNTIME_GUARD(envoy_reloadable_features_internal_address);
RUNTIME_GUARD(envoy_reloadable_features_listener_reuse_port_default_enabled);
RUNTIME_GUARD(envoy_reloadable_features_listener_wildcard_match_ip_family);
RUNTIME_GUARD(envoy_reloadable_features_new_tcp_connection_pool);
RUNTIME_GUARD(envoy_reloadable_features_postpone_h3_client_connect_to_next_loop);
RUNTIME_GUARD(envoy_reloadable_features_proxy_102_103);
RUNTIME_GUARD(envoy_reloadable_features_remove_legacy_json);
RUNTIME_GUARD(envoy_reloadable_features_sanitize_http_header_referer);
RUNTIME_GUARD(envoy_reloadable_features_skip_delay_close);
RUNTIME_GUARD(envoy_reloadable_features_skip_dispatching_frames_for_closed_connection);
RUNTIME_GUARD(envoy_reloadable_features_support_locality_update_on_eds_cluster_endpoints);
RUNTIME_GUARD(envoy_reloadable_features_test_feature_true);
RUNTIME_GUARD(envoy_reloadable_features_udp_listener_updates_filter_chain_in_place);
RUNTIME_GUARD(envoy_reloadable_features_update_expected_rq_timeout_on_retry);
RUNTIME_GUARD(envoy_reloadable_features_use_dns_ttl);
RUNTIME_GUARD(envoy_reloadable_features_validate_connect);
RUNTIME_GUARD(envoy_restart_features_explicit_wildcard_resource);
RUNTIME_GUARD(envoy_restart_features_use_apple_api_for_dns_lookups);

// Begin false flags. These should come with a TODO to flip true.
// Sentinel and test flag.
FALSE_RUNTIME_GUARD(envoy_reloadable_features_test_feature_false);
// TODO(alyssawilk, junr03) flip (and add release notes + docs) these after Lyft tests
FALSE_RUNTIME_GUARD(envoy_reloadable_features_allow_multiple_dns_addresses);
// TODO(adisuissa) reset to true to enable unified mux by default
FALSE_RUNTIME_GUARD(envoy_reloadable_features_unified_mux);
// TODO(alyssar) flip false once issue complete.
FALSE_RUNTIME_GUARD(envoy_restart_features_no_runtime_singleton);

// Block of non-boolean flags. These are deprecated. Do not add more.
ABSL_FLAG(uint64_t, envoy_buffer_overflow_multiplier, 0, "");                        // NOLINT
ABSL_FLAG(uint64_t, envoy_do_not_use_going_away_max_http2_outbound_response, 2, ""); // NOLINT
ABSL_FLAG(uint64_t, envoy_headermap_lazy_map_min_size, 3, "");                       // NOLINT
ABSL_FLAG(uint64_t, re2_max_program_size_error_level, 100, "");                      // NOLINT
ABSL_FLAG(uint64_t, re2_max_program_size_warn_level,                                 // NOLINT
          std::numeric_limits<uint32_t>::max(), "");                                 // NOLINT

namespace Envoy {
namespace Runtime {

class RuntimeFeatures {
public:
  RuntimeFeatures();

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

std::string swapPrefix(std::string name) {
  return absl::StrReplaceAll(name, {{"envoy_", "envoy."}, {"features_", "features."}});
}

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

bool isRuntimeFeature(absl::string_view feature) {
  return RuntimeFeaturesDefaults::get().getFlag(feature) != nullptr;
}

bool runtimeFeatureEnabled(absl::string_view feature) {
  auto* flag = RuntimeFeaturesDefaults::get().getFlag(feature);
  if (flag == nullptr) {
    IS_ENVOY_BUG(absl::StrCat("Unable to find runtime feature ", feature));
    return false;
  }
  // We validate in map creation that the flag is a boolean.
  return flag->TryGet<bool>().value();
}

uint64_t getInteger(absl::string_view feature, uint64_t default_value) {
  if (absl::StartsWith(feature, "envoy.")) {
    // DO NOT ADD MORE FLAGS HERE. This function deprecated and being removed.
    if (feature == "envoy.buffer.overflow_multiplier") {
      return absl::GetFlag(FLAGS_envoy_buffer_overflow_multiplier);
    } else if (feature == "envoy.do_not_use_going_away_max_http2_outbound_responses") {
      return absl::GetFlag(FLAGS_envoy_do_not_use_going_away_max_http2_outbound_response);
    } else if (feature == "envoy.http.headermap.lazy_map_min_size") {
      return absl::GetFlag(FLAGS_envoy_headermap_lazy_map_min_size);
    }
  }
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

void maybeSetRuntimeGuard(absl::string_view name, bool value) {
  auto* flag = RuntimeFeaturesDefaults::get().getFlag(name);
  if (flag == nullptr) {
    IS_ENVOY_BUG(absl::StrCat("Unable to find runtime feature ", name));
    return;
  }
  flag->ParseFrom(value ? "true" : "false", nullptr);
}

void maybeSetDeprecatedInts(absl::string_view name, uint32_t value) {
  if (!absl::StartsWith(name, "envoy.") && !absl::StartsWith(name, "re2.")) {
    return;
  }

  // DO NOT ADD MORE FLAGS HERE. This function deprecated and being removed.
  if (name == "envoy.buffer.overflow_multiplier") {
    absl::SetFlag(&FLAGS_envoy_buffer_overflow_multiplier, value);
  } else if (name == "envoy.do_not_use_going_away_max_http2_outbound_responses") {
    absl::SetFlag(&FLAGS_envoy_do_not_use_going_away_max_http2_outbound_response, value);
  } else if (name == "envoy.http.headermap.lazy_map_min_size") {
    absl::SetFlag(&FLAGS_envoy_headermap_lazy_map_min_size, value);
  } else if (name == "re2.max_program_size.error_level") {
    absl::SetFlag(&FLAGS_re2_max_program_size_error_level, value);
  } else if (name == "re2.max_program_size.warn_level") {
    absl::SetFlag(&FLAGS_re2_max_program_size_warn_level, value);
  }
}

} // namespace Runtime
} // namespace Envoy
