#include "source/extensions/tracers/opentelemetry/resource_detectors/static/static_config_resource_detector.h"

#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief Detects a resource from static config.
 *
 * @return Resource A resource with the attributes from static config.
 */
Resource StaticConfigResourceDetector::detect() {
  Resource resource;
  resource.schema_url_ = "";

  for (const auto& pair : config_.attributes()) {
    const std::string& key = pair.first;
    const std::string& value = pair.second;
    if (value.empty()) {
      ENVOY_LOG(warn, "Attribute {} cannot be empty.", key);
      continue;
    }
    resource.attributes_[key] = value;
  }
  return resource;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
