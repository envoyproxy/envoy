#pragma once

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/options.h"
#include "envoy/server/proactive_resource_monitor.h"
#include "envoy/server/resource_monitor.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class ResourceMonitorFactoryContext {
public:
  virtual ~ResourceMonitorFactoryContext() = default;

  /**
   * @return Event::Dispatcher& the main thread's dispatcher. This dispatcher should be used
   *         for all singleton processing.
   */
  virtual Event::Dispatcher& mainThreadDispatcher() PURE;

  /**
   * @return Server::Options& the command-line options that Envoy was started with.
   */
  virtual const Options& options() PURE;

  /**
   * @return reference to the Api object
   */
  virtual Api::Api& api() PURE;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for filter configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;
};

/**
 * Implemented by each resource monitor and registered via Registry::registerFactory()
 * or the convenience class RegistryFactory.
 */
class ResourceMonitorFactory : public Config::TypedFactory {
public:
  ~ResourceMonitorFactory() override = default;

  /**
   * Create a particular resource monitor implementation.
   * @param config const ProtoBuf::Message& supplies the config for the resource monitor
   *        implementation.
   * @param context ResourceMonitorFactoryContext& supplies the resource monitor's context.
   * @return ResourceMonitorPtr the resource monitor instance. Should not be nullptr.
   * @throw EnvoyException if the implementation is unable to produce an instance with
   *        the provided parameters.
   */
  virtual ResourceMonitorPtr createResourceMonitor(const Protobuf::Message& config,
                                                   ResourceMonitorFactoryContext& context) PURE;

  std::string category() const override { return "envoy.resource_monitors"; }
};

class ProactiveResourceMonitorFactory : public Config::TypedFactory {
public:
  ~ProactiveResourceMonitorFactory() override = default;

  /**
   * Create a particular proactive resource monitor implementation.
   * @param config const ProtoBuf::Message& supplies the config for the proactive resource monitor
   *        implementation.
   * @param context ResourceMonitorFactoryContext& supplies the resource monitor's context.
   * @return ProactiveResourceMonitorPtr the resource monitor instance. Should not be nullptr.
   * @throw EnvoyException if the implementation is unable to produce an instance with
   *        the provided parameters.
   */
  virtual ProactiveResourceMonitorPtr
  createProactiveResourceMonitor(const Protobuf::Message& config,
                                 ResourceMonitorFactoryContext& context) PURE;

  std::string category() const override { return "envoy.resource_monitors"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
