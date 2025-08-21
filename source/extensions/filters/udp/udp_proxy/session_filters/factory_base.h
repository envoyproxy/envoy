#pragma once

#include "envoy/server/filter_config.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

using FilterFactoryCb = Network::UdpSessionFilterFactoryCb;
using NamedUdpSessionFilterConfigFactory =
    Server::Configuration::NamedUdpSessionFilterConfigFactory;

template <class ConfigProto> class FactoryBase : public NamedUdpSessionFilterConfigFactory {
public:
  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    Server::Configuration::FactoryContext& context) PURE;

  const std::string name_;
};

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
