#pragma once

#include "envoy/config/trace/v3/opentelemetry.pb.h"

#include "resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

constexpr absl::string_view kServiceNameKey = "service.name";
constexpr absl::string_view kDefaultServiceName = "unknown_service:envoy";

class ResourceProvider : public Logger::Loggable<Logger::Id::tracing> {
public:
  virtual ~ResourceProvider() = default;

  /**
   * @brief Iterates through all loaded resource detectors and merge all the returned
   * resources into one. Resource merging is done according to the OpenTelemetry
   * resource SDK specification. @see
   * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#merge.
   *
   * @param opentelemetry_config The opentelemetry configuration, which contains the configured
   * resource detectors.
   * @param context The tracer factory context.
   * @return Resource const The merged resource.
   */
  virtual Resource const
  getResource(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
              Server::Configuration::TracerFactoryContext& context) const = 0;
};

class ResourceProviderImpl : public ResourceProvider {
public:
  Resource const
  getResource(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
              Server::Configuration::TracerFactoryContext& context) const override;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
