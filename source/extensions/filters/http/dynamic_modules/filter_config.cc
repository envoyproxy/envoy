#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

DynamicModuleHttpFilterConfig::DynamicModuleHttpFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    Extensions::DynamicModules::DynamicModuleSharedPtr dynamic_module)
    : filter_name_(filter_name), filter_config_(filter_config), dynamic_module_(dynamic_module){};

DynamicModuleHttpFilterConfig::~DynamicModuleHttpFilterConfig() = default;

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
