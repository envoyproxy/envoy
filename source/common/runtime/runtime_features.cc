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
    "envoy.reloadable_features.http1_flood_protection",
    "envoy.reloadable_features.test_feature_true",
    "envoy.reloadable_features.strict_header_validation",
    "envoy.reloadable_features.connection_header_sanitization",
    "envoy.reloadable_features.strict_authority_validation",
    "envoy.reloadable_features.reject_unsupported_transfer_encodings",
    "envoy.reloadable_features.strict_method_validation",
    "envoy.reloadable_features.new_http1_connection_pool_behavior",
    "envoy.reloadable_features.new_http2_connection_pool_behavior",
    "envoy.deprecated_features.allow_deprecated_extension_names",
    "envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher",
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
    // Sentinel and test flag.
    "envoy.reloadable_features.test_feature_false",
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
