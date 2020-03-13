#pragma once

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Runtime {

// Helper class for runtime-derived boolean feature flags.
class FeatureFlag {
public:
  FeatureFlag(const envoy::config::core::v3::RuntimeFeatureFlag& feature_flag_proto,
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
  FractionalPercent(
      const envoy::config::core::v3::RuntimeFractionalPercent& fractional_percent_proto,
      Runtime::Loader& runtime)
      : runtime_key_(fractional_percent_proto.runtime_key()),
        default_value_(fractional_percent_proto.default_value()), runtime_(runtime) {}

  bool enabled() const { return runtime_.snapshot().featureEnabled(runtime_key_, default_value_); }

private:
  const std::string runtime_key_;
  const envoy::type::v3::FractionalPercent default_value_;
  Runtime::Loader& runtime_;
};

} // namespace Runtime
} // namespace Envoy
