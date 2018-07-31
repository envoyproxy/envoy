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
  Server::ResourceMonitorPtr
  createResourceMonitor(const Protobuf::Message& config,
                        Server::Configuration::ResourceMonitorFactoryContext& context) override {
    return createResourceMonitorFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(config), context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const ConfigProto& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Common
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
