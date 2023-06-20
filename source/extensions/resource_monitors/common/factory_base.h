#pragma once

#include "envoy/server/resource_monitor_config.h"

#include "source/common/protobuf/utility.h"

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
    return createResourceMonitorFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                   config, context.messageValidationVisitor()),
                                               context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Server::ResourceMonitorPtr createResourceMonitorFromProtoTyped(
      const ConfigProto& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) PURE;

  const std::string name_;
};

template <class ConfigProto>
class ProactiveFactoryBase : public Server::Configuration::ProactiveResourceMonitorFactory {
public:
  Server::ProactiveResourceMonitorPtr createProactiveResourceMonitor(
      const Protobuf::Message& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override {
    return createProactiveResourceMonitorFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(config,
                                                             context.messageValidationVisitor()),
        context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  ProactiveFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Server::ProactiveResourceMonitorPtr createProactiveResourceMonitorFromProtoTyped(
      const ConfigProto& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Common
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
