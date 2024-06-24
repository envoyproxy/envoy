#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/health_checker.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class HealthCheckerFactoryContext {
public:
  virtual ~HealthCheckerFactoryContext() = default;

  /**
   * @return Upstream::Cluster& the owning cluster.
   */
  virtual Upstream::Cluster& cluster() PURE;

  /**
   * @return Runtime::Loader& the singleton runtime loader for the server.
   */
  virtual Envoy::Runtime::Loader& runtime() PURE;

  /**
   * @return Event::Dispatcher& the main thread's dispatcher. This dispatcher should be used
   *         for all singleton processing.
   */
  virtual Event::Dispatcher& mainThreadDispatcher() PURE;

  /*
   * @return Upstream::HealthCheckEventLoggerPtr the health check event logger for the
   * created health checkers. This function may not be idempotent.
   */
  virtual Upstream::HealthCheckEventLoggerPtr eventLogger() PURE;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for health checker configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;

  /**
   * @return Api::Api& the API used by the server.
   */
  virtual Api::Api& api() PURE;

  /**
   * @return AccessLogManager for use by the entire server.
   */
  virtual AccessLog::AccessLogManager& accessLogManager() PURE;

  /*
   * @return Server context.
   */
  virtual Server::Configuration::ServerFactoryContext& serverFactoryContext() PURE;

  /**
   * Set the event logger to the context, nullptr is accepted since
   * the default in the context is nullptr.
   * @param event_logger the health check event logger.
   */
  virtual void setEventLogger(Upstream::HealthCheckEventLoggerPtr event_logger) PURE;
};

/**
 * Implemented by each custom health checker and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class CustomHealthCheckerFactory : public Config::TypedFactory {
public:
  ~CustomHealthCheckerFactory() override = default;

  /**
   * Creates a particular custom health checker factory implementation.
   *
   * @param config supplies the configuration as a full envoy::config::core::v3::HealthCheck
   * config. The implementation of this method can get the specific configuration for a custom
   * health check from custom_health_check().config().
   * @param context supplies the custom health checker's context.
   * @return HealthCheckerSharedPtr the pointer of a health checker instance.
   */
  virtual Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            HealthCheckerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.health_checkers"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
