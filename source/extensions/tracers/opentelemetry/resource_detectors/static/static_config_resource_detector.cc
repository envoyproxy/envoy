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
StaticConfigResourceDetector::StaticConfigResourceDetector(
    const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
        StaticConfigResourceDetectorConfig& config,
    Server::Configuration::ServerFactoryContext&) {
  auto resource = std::make_shared<Resource>();

  for (const auto& [key, value] : config.attributes()) {
    if (value.empty()) {

      ENVOY_LOG(warn, "Attribute {} cannot be empty.", key);
      continue;
    }
    resource->attributes_[key] = value;
  }
  resource_ = resource;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
