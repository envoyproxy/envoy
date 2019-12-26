#pragma once

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"

namespace Envoy {
namespace Config {

// Translate from old fixed runtime to new layered runtime configuration.
void translateRuntime(const envoy::config::bootstrap::v2::Runtime& runtime_config,
                      envoy::config::bootstrap::v2::LayeredRuntime& layered_runtime_config);

} // namespace Config
} // namespace Envoy
