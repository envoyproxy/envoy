#pragma once

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/server/resource_monitor.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented by each resource monitor and registered via Registry::registerFactory()
 * or the convenience class RegistryFactory.
 */
class ResourceMonitorFactory {
public:
  virtual ~ResourceMonitorFactory() {}

  /**
   * Create a particular resource monitor implementation.
   * @param config const ProtoBuf::Message& supplies the config for the resource monitor
   *        implementation.
   * @param dispatcher Event::Dispatcher& the main thread's dispatcher. This is used by
   *        resource monitors that need to make asynchronous calls.
   * @return ResourceMonitorPtr the resource monitor instance. Should not be nullptr.
   * @throw EnvoyException if the implementation is unable to produce an instance with
   *        the provided parameters.
   */
  virtual ResourceMonitorPtr createResourceMonitor(const Protobuf::Message& config,
                                                   Event::Dispatcher& dispatcher) PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a resource
   * monitor produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
