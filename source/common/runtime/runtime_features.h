#pragma once

#include <string>

#include "envoy/runtime/runtime.h"

#include "source/common/singleton/const_singleton.h"

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"

namespace Envoy {
namespace Runtime {

bool isRuntimeFeature(absl::string_view feature);
bool runtimeFeatureEnabled(absl::string_view feature);
uint64_t getInteger(absl::string_view feature, uint64_t default_value);

void maybeSetRuntimeGuard(absl::string_view name, bool value);

void maybeSetDeprecatedInts(absl::string_view name, uint32_t value);
constexpr absl::string_view conn_pool_new_stream_with_early_data_and_http3 =
    "envoy.reloadable_features.conn_pool_new_stream_with_early_data_and_http3";

class RuntimeFeatures {
public:
  RuntimeFeatures();

  void restoreDefaults() const;

private:
  friend class RuntimeFeaturesPeer;
  mutable std::unique_ptr<absl::FlagSaver> saver_;
};

using RuntimeFeaturesDefaults = ConstSingleton<RuntimeFeatures>;

} // namespace Runtime
} // namespace Envoy
