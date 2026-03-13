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
std::shared_ptr<Resource> createInitialResource(absl::string_view service_name) {
  auto resource = std::make_shared<Resource>();

  // Creates initial resource with the static service.name and telemetry.sdk.* attributes.
  if (!service_name.empty()) {
    resource->attributes_[kServiceNameKey] = service_name;
  }
  resource->attributes_[kTelemetrySdkLanguageKey] = kDefaultTelemetrySdkLanguage;

  resource->attributes_[kTelemetrySdkNameKey] = kDefaultTelemetrySdkName;

  resource->attributes_[kTelemetrySdkVersionKey] = Envoy::VersionInfo::version();

  return resource;
}
} // namespace

ResourceProviderImpl::ResourceProviderImpl(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>&
        resource_detectors,
    Envoy::Server::Configuration::ServerFactoryContext& context, absl::string_view service_name)
    : context_(context), service_name_(service_name) {
  for (const auto& detector_config : resource_detectors) {
    auto* factory = Envoy::Config::Utility::getFactory<ResourceDetectorFactory>(detector_config);
    if (!factory) {
      throw EnvoyException(
          fmt::format("Resource detector factory not found: '{}'", detector_config.name()));
    }
    auto detector = factory->createResourceDetector(detector_config.typed_config(), context_);
    if (!detector) {
      throw EnvoyException(
          fmt::format("Resource detector could not be created: '{}'", detector_config.name()));
    }
    resource_detectors_.push_back(std::move(detector));
  }
  std::shared_ptr<Resource> resource = createInitialResource(service_name_);

  for (const auto& detector : resource_detectors_) {
    ResourceConstSharedPtr detected_resource = detector->detect();
    if (detected_resource != nullptr) {
      resource->merge(*detected_resource);
    }
  }
  global_resource_ = resource;
}

ResourceConstSharedPtr
ResourceProviderImpl::getResource(const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& detector : resource_detectors_) {
    ResourceConstSharedPtr detected_resource = detector->detect(stream_info);
    if (detected_resource != nullptr) {
      return detected_resource;
    }
  }
  return nullptr;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
