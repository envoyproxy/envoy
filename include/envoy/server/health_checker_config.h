#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/health_checker.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class HealthCheckerFactoryContext {
public:
  virtual ~HealthCheckerFactoryContext() {}

  /**
   * @return Upstream::Cluster& the owning cluster.
   */
  virtual Upstream::Cluster& cluster() PURE;

  /**
   * @return Runtime::Loader& the singleton runtime loader for the server.
   */
  virtual Envoy::Runtime::Loader& runtime() PURE;

  /**
   * @return RandomGenerator& the random generator for the server.
   */
  virtual Envoy::Runtime::RandomGenerator& random() PURE;

  /**
   * @return Event::Dispatcher& the main thread's dispatcher. This dispatcher should be used
   *         for all singleton processing.
   */
  virtual Event::Dispatcher& dispatcher() PURE;
};

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
   * @param config supplies the configuration as a full envoy::api::v2::core::HealthCheck config.
   *        The implementation of this method can get the specific configuration for a custom health
   *        check from custom_health_check().config().
   * @param context supplies the custom health checker's context.
   * @return HealthCheckerSharedPtr the pointer of a health checker instance.
   */
  virtual Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::api::v2::core::HealthCheck& config,
                            HealthCheckerFactoryContext& context) PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a custom health
   * checker produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy