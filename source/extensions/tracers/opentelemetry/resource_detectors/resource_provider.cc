#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h"

#include <string>

#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/version/version.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {
bool isEmptyResource(const Resource& resource) { return resource.attributes_.empty(); }

Resource createInitialResource(absl::string_view service_name) {
  Resource resource{};

  // Creates initial resource with the static service.name and telemetry.sdk.* attributes.
  if (!service_name.empty()) {
    resource.attributes_[kServiceNameKey] = service_name;
  }
  resource.attributes_[kTelemetrySdkLanguageKey] = kDefaultTelemetrySdkLanguage;

  resource.attributes_[kTelemetrySdkNameKey] = kDefaultTelemetrySdkName;

  resource.attributes_[kTelemetrySdkVersionKey] = Envoy::VersionInfo::version();

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
  // The schemaUrl is merged, regardless if the resources being merged
  // have attributes or not. This behavior is compliant with the OTel spec.
  // see: https://github.com/envoyproxy/envoy/pull/29547#discussion_r1344540427
  old_resource.schema_url_ =
      resolveSchemaUrl(old_resource.schema_url_, updating_resource.schema_url_);

  if (isEmptyResource(updating_resource)) {
    return;
  }
  for (auto const& attr : updating_resource.attributes_) {
    old_resource.attributes_.insert_or_assign(attr.first, attr.second);
  }
}
} // namespace

Resource ResourceProviderImpl::getResource(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>&
        resource_detectors,
    Envoy::Server::Configuration::ServerFactoryContext& context,
    absl::string_view service_name) const {

  Resource resource = createInitialResource(service_name);

  for (const auto& detector_config : resource_detectors) {
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
