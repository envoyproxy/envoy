#pragma once

#include "envoy/server/resource_monitor_config.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace Common {

template <class ConfigProto>
class FactoryBase : public Server::Configuration::ResourceMonitorFactory {
public:
  Server::ResourceMonitorPtr createResourceMonitor(const Protobuf::Message& config,
                                                   Event::Dispatcher& dispatcher) override {
    return createResourceMonitorFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(config), dispatcher);
  }

  std::string name() override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Server::ResourceMonitorPtr
  createResourceMonitorFromProtoTyped(const ConfigProto& config,
                                      Event::Dispatcher& dispatcher) PURE;

  const std::string name_;
};

} // namespace Common
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
