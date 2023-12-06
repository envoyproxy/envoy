#pragma once

#include <map>
#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/server/tracer_config.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief A string key-value map that stores the resource attributes.
 */
using ResourceAttributes = std::map<std::string, std::string>;

/**
 * @brief A Resource represents the entity producing telemetry as Attributes.
 * For example, a process producing telemetry that is running in a container on Kubernetes
 * has a Pod name, it is in a namespace and possibly is part of a Deployment which also has a name.
 * See:
 * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.26.0/specification/resource/sdk.md
 */
struct Resource {
  std::string schema_url_{""};
  ResourceAttributes attributes_{};

  virtual ~Resource() = default;
};

using ResourceConstSharedPtr = std::shared_ptr<const Resource>;

/**
 * @brief The base type for all resource detectors
 *
 */
class ResourceDetector {
public:
  virtual ~ResourceDetector() = default;

  /**
   * @brief Load attributes and returns a Resource object
   * populated with them and a possible SchemaUrl.
   * @return Resource
   */
  virtual Resource detect() PURE;
};

using ResourceDetectorPtr = std::unique_ptr<ResourceDetector>;

/*
 * A factory for creating resource detectors.
 */
class ResourceDetectorFactory : public Envoy::Config::TypedFactory {
public:
  ~ResourceDetectorFactory() override = default;

  /**
   * @brief Creates a resource detector based on the configuration type provided.
   *
   * @param message The resource detector configuration.
   * @param context The tracer factory context.
   * @return ResourceDetectorPtr A resource detector based on the configuration type provided.
   */
  virtual ResourceDetectorPtr
  createResourceDetector(const Protobuf::Message& message,
                         Server::Configuration::TracerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers.opentelemetry.resource_detectors"; }
};

using ResourceDetectorTypedFactoryPtr = std::unique_ptr<ResourceDetectorFactory>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
