#pragma once

#include "envoy/config/core/v3/base.pb.h"

#include "source/extensions/dynamic_modules/abi/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Look up a string value in dynamic metadata by filter name and dotted key path.
bool getDynamicMetadataStringByPath(const envoy::config::core::v3::Metadata& metadata,
                                    envoy_dynamic_module_type_module_buffer filter_name,
                                    envoy_dynamic_module_type_module_buffer path,
                                    envoy_dynamic_module_type_envoy_buffer* result);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
