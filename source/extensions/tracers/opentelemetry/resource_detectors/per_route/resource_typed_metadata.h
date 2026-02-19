#pragma once

#ifndef THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_RESOURCE_TYPED_METADATA_H_
#define THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_RESOURCE_TYPED_METADATA_H_

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/per_route_resource_metadata.pb.h"
#include "envoy/router/router.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"
namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class ResourceTypedMetadata : public Config::TypedMetadata::Object {
public:
  ResourceTypedMetadata(const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
                            PerRouteResourceMetadata& config);

  ResourceConstSharedPtr resource() const { return resource_; }

private:
  ResourceConstSharedPtr resource_;
};

class ResourceTypedRouteMetadataFactory : public Router::HttpRouteTypedMetadataFactory {
public:
  static constexpr char kName[] = "envoy.tracers.opentelemetry.resource_typed_metadata";
  std::string name() const override { return kName; }

  std::unique_ptr<const Config::TypedMetadata::Object>
  parse(const Protobuf::Struct&) const override {
    throw EnvoyException("Struct parsing not supported for ResourceTypedRouteMetadataFactory");
  }

  std::unique_ptr<const Config::TypedMetadata::Object>
  parse(const Protobuf::Any& data) const override;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy

#endif // THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_TRACERS_OPENTELEMETRY_RESOURCE_DETECTORS_PER_ROUTE_RESOURCE_TYPED_METADATA_H_
