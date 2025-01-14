#include "contrib/fluentd_tracer/source/config.h"

#include <memory>

#include "contrib/envoy/extensions/tracers/fluentd/v3alpha/fluentd.pb.h"
#include "contrib/envoy/extensions/tracers/fluentd/v3alpha/fluentd.pb.validate.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "contrib/fluentd_tracer/source/fluentd_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(fluentd_tracer_cache);

FluentdTracerCacheSharedPtr FluentdTracerFactory::getTracerCacheSingleton(
    Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<FluentdTracerCacheImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(fluentd_tracer_cache),
      [&context] {
        return std::make_shared<FluentdTracerCacheImpl>(context.clusterManager(), context.scope(),
                                                        context.threadLocal());
      },
      /* pin = */ true);
}

FluentdTracerFactory::FluentdTracerFactory() : FactoryBase("envoy.tracers.fluentd") {}

Tracing::DriverSharedPtr FluentdTracerFactory::createTracerDriverTyped(
    const envoy::extensions::tracers::fluentd::v3alpha::FluentdConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<Driver>(std::make_shared<FluentdConfig>(proto_config), context,
                                  getTracerCacheSingleton(context.serverFactoryContext()));
}

/**
 * Static registration for the Fluentd tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(FluentdTracerFactory, Server::Configuration::TracerFactory);

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
