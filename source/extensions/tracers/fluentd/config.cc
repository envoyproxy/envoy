#include "source/extensions/tracers/fluentd/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"

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
    const envoy::extensions::tracers::fluentd::v3::FluentdConfig& proto_config,
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
