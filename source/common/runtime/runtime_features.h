#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/runtime/runtime.h"

#include "common/protobuf/utility.h"
#include "common/singleton/const_singleton.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Runtime {

class RuntimeFeatures {
public:
  RuntimeFeatures();

  // This tracks proto configured features, to determine if a given deprecated
  // feature is still allowed, or has been made fatal-by-default per the Envoy
  // deprecation process.
  bool disallowedByDefault(absl::string_view feature) const {
    return disallowed_features_.find(feature) != disallowed_features_.end();
  }

  // This tracks config-guarded code paths, to determine if a given
  // runtime-guarded-code-path has the new code run by default or the old code.
  bool enabledByDefault(absl::string_view feature) const {
    return enabled_features_.find(feature) != enabled_features_.end();
  }

private:
  friend class RuntimeFeaturesPeer;

  absl::flat_hash_set<std::string> disallowed_features_;
  absl::flat_hash_set<std::string> enabled_features_;
};

using RuntimeFeaturesDefaults = ConstSingleton<RuntimeFeatures>;

// Helper class for runtime-derived boolean feature flags.
class FeatureFlag {
public:
  FeatureFlag(const envoy::api::v2::core::RuntimeFeatureFlag& feature_flag_proto,
              Runtime::Loader& runtime)
      : runtime_key_(feature_flag_proto.runtime_key()),
        default_value_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(feature_flag_proto, default_value, true)),
        runtime_(runtime) {}

  bool enabled() const { return runtime_.snapshot().getBoolean(runtime_key_, default_value_); }

private:
  const std::string runtime_key_;
  const bool default_value_;
  Runtime::Loader& runtime_;
};

// Helper class for runtime-derived fractional percent flags.
class FractionalPercent {
public:
  FractionalPercent(const envoy::api::v2::core::RuntimeFractionalPercent& fractional_percent_proto,
                    Runtime::Loader& runtime)
      : runtime_key_(fractional_percent_proto.runtime_key()),
        default_value_(fractional_percent_proto.default_value()), runtime_(runtime) {}

  bool enabled() const { return runtime_.snapshot().featureEnabled(runtime_key_, default_value_); }

private:
  const std::string runtime_key_;
  const envoy::type::FractionalPercent default_value_;
  Runtime::Loader& runtime_;
};

} // namespace Runtime
} // namespace Envoy
