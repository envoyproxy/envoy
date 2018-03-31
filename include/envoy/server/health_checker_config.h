#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/health_checker.h"

namespace Envoy {
namespace Upstream {

/**
 * Implemented by each extension health checker and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class ExtensionHealthCheckerFactory {
public:
  virtual ~ExtensionHealthCheckerFactory() {}

  /**
   * Creates a particular extension health checker factory implementation.
   *
   * @param config supplies the configuration for the health check, which should contains
   * extension_health_check.
   * @param cluster the upstream cluster.
   * @param runtime
   * @param random
   * @param dispatcher
   * @return HealthCheckerSharedPtr the pointer of a health checker instance.
   */
  virtual HealthCheckerSharedPtr createExtensionHealthChecker(const Protobuf::Message& config,
                                                              Upstream::Cluster& cluster,
                                                              Runtime::Loader& runtime,
                                                              Runtime::RandomGenerator& random,
                                                              Event::Dispatcher& dispatcher) PURE;
  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The filter
   *         config, which arrives in an opaque google.protobuf.Struct message, will be converted to
   *         JSON and then parsed into this empty proto. Optional today, will be compulsory when v1
   *         is deprecated.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of an extension health
   * checker produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Upstream
} // namespace Envoy
