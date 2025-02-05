#pragma once

#include "source/extensions/tracers/common/factory_base.h"

#include "contrib/envoy/extensions/tracers/fluentd/v3/fluentd.pb.h"
#include "contrib/envoy/extensions/tracers/fluentd/v3/fluentd.pb.validate.h"
#include "contrib/fluentd_tracer/source/fluentd_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

/**
 * Config registration for the Fluentd tracer. @see TracerFactory.
 */
class FluentdTracerFactory
    : public Common::FactoryBase<envoy::extensions::tracers::fluentd::v3::FluentdConfig> {
public:
  FluentdTracerFactory();

  static FluentdTracerCacheSharedPtr
  getTracerCacheSingleton(Server::Configuration::ServerFactoryContext& context);

private:
  // FactoryBase
  Tracing::DriverSharedPtr createTracerDriverTyped(
      const envoy::extensions::tracers::fluentd::v3::FluentdConfig& proto_config,
      Server::Configuration::TracerFactoryContext& context) override;
};

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
