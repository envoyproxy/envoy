#pragma once

#ifndef THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_CONFIG_H_
#define THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_CONFIG_H_

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/per_route_resource_detector.pb.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/per_route/per_route_resource_detector.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class PerRouteResourceDetectorFactory : public ResourceDetectorFactory {
public:
  /**
   * @brief Create a Resource Detector that reads from the OTEL_RESOURCE_ATTRIBUTES
   * environment variable.
   *
   * @param message The resource detector configuration.
   * @param context The tracer factory context.
   * @return ResourceDetectorPtr
   */
  ResourceDetectorPtr
  createResourceDetector(const Protobuf::Message&,
                         Server::Configuration::ServerFactoryContext&) override {
    return std::make_unique<PerRouteResourceDetector>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
                                PerRouteResourceDetectorConfig>();
  }

  std::string name() const override {
    return "envoy.tracers.opentelemetry.resource_detectors.per_route";
  }
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

#endif // THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_CONFIG_H_
