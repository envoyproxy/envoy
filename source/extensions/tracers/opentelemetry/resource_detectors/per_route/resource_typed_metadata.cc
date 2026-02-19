#include "source/extensions/tracers/opentelemetry/resource_detectors/per_route/resource_typed_metadata.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

ResourceTypedMetadata::ResourceTypedMetadata(
    const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
        PerRouteResourceMetadata& config) {
  auto resource = std::make_shared<Resource>();
  resource->schema_url_ = config.schema_url();
  for (const auto& [key, value] : config.attributes()) {
    resource->attributes_[key] = value;
  }
  resource_ = resource;
}

std::unique_ptr<const Config::TypedMetadata::Object>
ResourceTypedRouteMetadataFactory::parse(const Protobuf::Any& data) const {
  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::PerRouteResourceMetadata proto;
  data.UnpackTo(&proto);
  return std::make_unique<ResourceTypedMetadata>(proto);
}

static Registry::RegisterFactory<ResourceTypedRouteMetadataFactory, Config::TypedMetadataFactory>
    register_;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
