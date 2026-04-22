#pragma once

#include "envoy/config/trace/v3/opentelemetry.pb.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

constexpr absl::string_view kServiceNameKey = "service.name";
constexpr absl::string_view kDefaultServiceName = "unknown_service:envoy";

constexpr absl::string_view kTelemetrySdkLanguageKey = "telemetry.sdk.language";
constexpr absl::string_view kDefaultTelemetrySdkLanguage = "cpp";

constexpr absl::string_view kTelemetrySdkNameKey = "telemetry.sdk.name";
constexpr absl::string_view kDefaultTelemetrySdkName = "envoy";

constexpr absl::string_view kTelemetrySdkVersionKey = "telemetry.sdk.version";

class ResourceProvider : public Logger::Loggable<Logger::Id::tracing> {
public:
  virtual ~ResourceProvider() = default;

  /**
   * @brief Iterates through all loaded resource detectors and merge all the returned
   * resources into one. Resource merging is done according to the OpenTelemetry
   * resource SDK specification. @see
   * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#merge.
   *
   * @param opentelemetry_config The OpenTelemetry configuration, which contains the configured
   * resource detectors.
   * @param context The tracer factory context.
   * @return Resource const The merged resource.
   */
  virtual Resource
  getResource(const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>&
                  resource_detectors,
              Server::Configuration::ServerFactoryContext& context,
              absl::string_view service_name) const PURE;
};
using ResourceProviderPtr = std::shared_ptr<ResourceProvider>;

class ResourceProviderImpl : public ResourceProvider {
public:
  Resource
  getResource(const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>&
                  resource_detectors,
              Server::Configuration::ServerFactoryContext& context,
              absl::string_view service_name) const override;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
