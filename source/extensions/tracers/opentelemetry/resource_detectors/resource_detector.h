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
using ResourceAttributes = absl::flat_hash_map<std::string, std::string>;

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

  /**
   * @brief Updates this resource with a new one. This function implements
   * the Merge operation defined in the OpenTelemetry Resource SDK specification.
   * @see
   * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#merge
   *
   * @param other The new resource.
   */
  void merge(const Resource& other);

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
   * @return Resource. It is expected that the shared pointer returned is
   * constructed once and the same over the lifetime of the ResourceDetector.
   */
  virtual ResourceConstSharedPtr detect() const PURE;

  /**
   * @brief Detects the resource based on the stream info given.
   * @param stream_info The stream info to detect the resource detector for.
   * @return Resource shared pointer.
   *
   * The expectation is that the returned Resource will be persistently attached
   * to a route or cluster and reused for the lifetime of that route or cluster.
   * This allows for the resource to be attached to a span and for spans to be
   * keyed by resource without additional hashing overhead.
   */
  virtual ResourceConstSharedPtr detect(const StreamInfo::StreamInfo& stream_info) const PURE;
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
                         Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers.opentelemetry.resource_detectors"; }
};

using ResourceDetectorTypedFactoryPtr = std::unique_ptr<ResourceDetectorFactory>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
