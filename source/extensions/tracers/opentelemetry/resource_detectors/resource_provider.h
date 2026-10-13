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

/**
 * @brief Configuration options controlling the population of optional attributes in the emitted
 * OpenTelemetry resource object.
 */
struct ResourceProviderOptions {
  /**
   * @brief Whether to automatically include telemetry SDK metadata attributes
   * (`telemetry.sdk.language`, `telemetry.sdk.name`, `telemetry.sdk.version`).
   */
  bool set_telemetry_sdk_resource_attributes{true};
  /**
   * @brief Whether to automatically include the `service.name` resource attribute.
   */
  bool set_service_name_resource_attribute{true};

  bool operator==(const ResourceProviderOptions& other) const = default;
};

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
              Server::Configuration::ServerFactoryContext& context, absl::string_view service_name,
              const ResourceProviderOptions& options) const PURE;
};
using ResourceProviderPtr = std::shared_ptr<ResourceProvider>;

class ResourceProviderImpl : public ResourceProvider {
public:
  Resource
  getResource(const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>&
                  resource_detectors,
              Server::Configuration::ServerFactoryContext& context, absl::string_view service_name,
              const ResourceProviderOptions& options) const override;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
