#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/health_checker_config.h"

namespace Envoy {
namespace Upstream {

/**
 * Sink for health check event.
 */
class HealthCheckEventSink {
public:
  virtual ~HealthCheckEventSink() = default;

  virtual void log(envoy::data::core::v3::HealthCheckEvent event) PURE;
};

using HealthCheckEventSinkPtr = std::unique_ptr<HealthCheckEventSink>;
using HealthCheckEventSinkSharedPtr = std::shared_ptr<HealthCheckEventSink>;

/**
 * A factory abstract class for creating instances of HealthCheckEventSink.
 */
class HealthCheckEventSinkFactory : public Config::TypedFactory {
public:
  ~HealthCheckEventSinkFactory() override = default;

  /**
   * Creates an HealthCheckEventSink using the given config.
   */
  virtual HealthCheckEventSinkPtr
  createHealthCheckEventSink(const ProtobufWkt::Any& config,
                             Server::Configuration::HealthCheckerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.health_check.event_sinks"; }
};

} // namespace Upstream
} // namespace Envoy
