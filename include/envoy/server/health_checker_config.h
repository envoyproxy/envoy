#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/health_checker.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented by each custom health checker and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class CustomHealthCheckerFactory {
public:
  virtual ~CustomHealthCheckerFactory() {}

  /**
   * Creates a particular custom health checker factory implementation.
   *
   * @param config supplies the configuration for the health check, which should contains
   * custom_health_check.
   * @param cluster the upstream cluster.
   * @param runtime supplies the runtime loader.
   * @param random supplies the random generator.
   * @param dispatcher supplies the dispatcher.
   * @return HealthCheckerSharedPtr the pointer of a health checker instance.
   */
  virtual Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const Protobuf::Message& config, Upstream::Cluster& cluster,
                            Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                            Event::Dispatcher& dispatcher) PURE;
  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message which arrives in as an
   * opaque google.protobuf.Struct message.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * @return std::string the identifying name for a particular implementation of an custom health
   * checker produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy