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

class Resource {
public:
  std::string schemaUrl{""};
  ResourceAttributes attributes{};

  virtual ~Resource() = default;
};

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
  virtual Resource detect() = 0;
};

using ResourceDetectorPtr = std::shared_ptr<ResourceDetector>;

/*
 * A factory for creating resource detectors that have configuration.
 */
class ResourceDetectorTypedFactory : public Envoy::Config::TypedFactory {
public:
  ~ResourceDetectorTypedFactory() override = default;

  /**
   * @brief Creates a resource detector based on the configuration type provided.
   *
   * @param message The resource detector configuration.
   * @param context The tracer factory context.
   * @return ResourceDetectorPtr A resource detector based on the configuration type provided.
   */
  virtual ResourceDetectorPtr
  createTypedResourceDetector(const Protobuf::Message& message,
                              Server::Configuration::TracerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers.opentelemetry.resource_detectors"; }
};

using ResourceDetectorTypedFactoryPtr = std::unique_ptr<ResourceDetectorTypedFactory>;

/*
 * A factory for creating resource detectors without configuration.
 */
class ResourceDetectorFactory : public Envoy::Config::UntypedFactory {
public:
  ~ResourceDetectorFactory() override = default;

  /**
   * @brief Creates a resource detector that does not have a configuration.
   *
   * @param context The tracer factory context.
   * @return ResourceDetectorPtr A resource detector based on the provided name.
   */
  virtual ResourceDetectorPtr
  createResourceDetector(Server::Configuration::TracerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers.opentelemetry.resource_detectors"; }
};

using ResourceDetectorFactoryPtr = std::unique_ptr<ResourceDetectorFactory>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
