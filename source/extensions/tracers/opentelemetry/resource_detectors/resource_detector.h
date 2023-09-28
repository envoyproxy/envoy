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

using ResourceAttributes = std::map<std::string, std::string>;

struct Resource {
  std::string schemaUrl_{""};
  ResourceAttributes attributes_{};

  virtual ~Resource() = default;
};

using ResourcePtr = std::unique_ptr<const Resource>;

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
