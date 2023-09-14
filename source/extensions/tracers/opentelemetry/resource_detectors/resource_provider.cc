#include "resource_provider.h"

#include <string>

#include "source/common/common/logger.h"
#include "source/common/config/utility.h"

#include "resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {
bool isEmptyResource(const Resource& resource) { return resource.attributes_.empty(); }

Resource createInitialResource(std::string service_name) {
  Resource resource{};

  // Creates initial resource with the static service.name attribute.
  if (service_name.empty()) {
    service_name = std::string{kDefaultServiceName};
  }
  resource.attributes_[std::string(kServiceNameKey.data(), kServiceNameKey.size())] = service_name;
  return resource;
}

/**
 * @brief Resolves the new schema url when merging two resources.
 * This function implements the algorithm as defined in the OpenTelemetry Resource SDK
 * specification. @see
 * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#merge
 *
 * @param old_schema_url The old resource's schema URL.
 * @param updating_schema_url The updating resource's schema URL.
 * @return std::string The calculated schema URL.
 */
std::string resolveSchemaUrl(const std::string& old_schema_url,
                             const std::string& updating_schema_url) {
  if (old_schema_url.empty()) {
    return updating_schema_url;
  }
  if (updating_schema_url.empty()) {
    return old_schema_url;
  }
  if (old_schema_url == updating_schema_url) {
    return old_schema_url;
  }
  // The OTel spec leaves this case (when both have value but are different) unspecified.
  ENVOY_LOG_MISC(info, "Resource schemaUrl conflict. Fall-back to old schema url: {}",
                 old_schema_url);
  return old_schema_url;
}

/**
 * @brief Updates an old resource with a new one. This function implements
 * the Merge operation defined in the OpenTelemetry Resource SDK specification.
 * @see
 * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#merge
 *
 * @param old_resource The old resource.
 * @param updating_resource The new resource.
 */
void mergeResource(Resource& old_resource, const Resource& updating_resource) {
  if (isEmptyResource(updating_resource)) {
    return;
  }
  for (auto const& attr : updating_resource.attributes_) {
    old_resource.attributes_.insert_or_assign(attr.first, attr.second);
  }
  old_resource.schemaUrl_ = resolveSchemaUrl(old_resource.schemaUrl_, updating_resource.schemaUrl_);
}
} // namespace

Resource ResourceProviderImpl::getResource(
    const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
    Server::Configuration::TracerFactoryContext& context) const {

  Resource resource = createInitialResource(opentelemetry_config.service_name());

  const auto& detectors_configs = opentelemetry_config.resource_detectors();

  for (const auto& detector_config : detectors_configs) {
    ResourceDetectorPtr detector;
    auto* factory = Envoy::Config::Utility::getFactory<ResourceDetectorFactory>(detector_config);

    if (!factory) {
      throw EnvoyException(
          fmt::format("Resource detector factory not found: '{}'", detector_config.name()));
    }

    detector = factory->createResourceDetector(detector_config.typed_config(), context);

    if (!detector) {
      throw EnvoyException(
          fmt::format("Resource detector could not be created: '{}'", detector_config.name()));
    }

    Resource detected_resource = detector->detect();
    mergeResource(resource, detected_resource);
  }
  return resource;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
