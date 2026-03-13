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
   * @brief Returns the resource held by this provider.
   * @return Resource const The merged resource.
   */
  virtual ResourceConstSharedPtr getResource() const PURE;

  virtual ResourceConstSharedPtr getResource(const StreamInfo::StreamInfo& stream_info) const PURE;
};
using ResourceProviderPtr = std::shared_ptr<ResourceProvider>;

class ResourceProviderImpl : public ResourceProvider {
public:
  /**
   * @brief Iterates through all loaded resource detectors and merge all the
   * returned resources into one. Resource merging is done according to the
   * OpenTelemetry resource SDK specification. @see
   * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#merge.*/
  ResourceProviderImpl(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>&
          resource_detectors,
      Server::Configuration::ServerFactoryContext& context, absl::string_view service_name);

  ResourceConstSharedPtr getResource() const override { return global_resource_; }

  /**
   * @brief Iterates through all loaded resource detectors. As soon as a resource
   * detector returns a non-null resource, that resource is returned. Currently,
   * no merging is done for stream-specific resources, favoring a "first detector
   * wins" approach for simplicity and performance. While merging, similar to
   * global resources, was considered for more advanced use cases, the current
   * implementation prioritizes a clear source for stream-specific attributes.
   */
  ResourceConstSharedPtr getResource(const StreamInfo::StreamInfo& stream_info) const override;

private:
  std::vector<ResourceDetectorPtr> resource_detectors_;
  Envoy::Server::Configuration::ServerFactoryContext& context_;
  std::string service_name_;
  ResourceConstSharedPtr global_resource_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
