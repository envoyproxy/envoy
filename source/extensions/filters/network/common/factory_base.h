#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {

/**
 * Common base class for network filter factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto, class ProtocolOptionsProto = ConfigProto>
class FactoryBase : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<ProtocolOptionsProto>();
  }

  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsConfig(
      const Protobuf::Message& proto_config,
      Server::Configuration::ProtocolOptionsFactoryContext& factory_context) override {
    return createProtocolOptionsTyped(MessageUtil::downcastAndValidate<const ProtocolOptionsProto&>(
                                          proto_config, factory_context.messageValidationVisitor()),
                                      factory_context);
  }

  std::string name() const override { return name_; }

  bool isTerminalFilterByProto(const Protobuf::Message& proto_config,
                               Server::Configuration::ServerFactoryContext& context) override {
    return isTerminalFilterByProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                            proto_config, context.messageValidationVisitor()),
                                        context);
  }

protected:
  FactoryBase(const std::string& name, bool is_terminal = false)
      : name_(name), is_terminal_filter_(is_terminal) {}

private:
  virtual bool isTerminalFilterByProtoTyped(const ConfigProto&,
                                            Server::Configuration::ServerFactoryContext&) {
    return is_terminal_filter_;
  }
  virtual Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    Server::Configuration::FactoryContext& context) PURE;

  virtual Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsTyped(const ProtocolOptionsProto&,
                             Server::Configuration::ProtocolOptionsFactoryContext&) {
    ExceptionUtil::throwEnvoyException(
        fmt::format("filter {} does not support protocol options", name_));
  }

  const std::string name_;
  const bool is_terminal_filter_;
};

} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
