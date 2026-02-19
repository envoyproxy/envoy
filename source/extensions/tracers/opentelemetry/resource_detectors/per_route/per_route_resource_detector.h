#pragma once

#ifndef THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_PER_ROUTE_RESOURCE_DETECTOR_H_
#define THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_PER_ROUTE_RESOURCE_DETECTOR_H_

#include "envoy/stream_info/stream_info.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/per_route/resource_typed_metadata.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class PerRouteResourceDetector : public ResourceDetector {
public:
  PerRouteResourceDetector() = default;
  // No global resource exists for this detector.
  ResourceConstSharedPtr detect() const override { return nullptr; }
  ResourceConstSharedPtr detect(const StreamInfo::StreamInfo& stream_info) const override {
    if (stream_info.route() == nullptr) {
      return nullptr;
    }
    const auto* metadata = stream_info.route()->typedMetadata().get<ResourceTypedMetadata>(
        ResourceTypedRouteMetadataFactory::kName);
    if (metadata == nullptr) {
      return nullptr;
    }
    return metadata->resource();
  }
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

#endif // THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_PER_ROUTE_RESOURCE_DETECTOR_H_
