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
class FactoryCommon : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<ProtocolOptionsProto>();
  }

  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsConfig(
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
  FactoryCommon(const std::string& name, bool is_terminal = false)
      : name_(name), is_terminal_filter_(is_terminal) {}

private:
  virtual bool isTerminalFilterByProtoTyped(const ConfigProto&,
                                            Server::Configuration::ServerFactoryContext&) {
    return is_terminal_filter_;
  }

  virtual absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr>
  createProtocolOptionsTyped(const ProtocolOptionsProto&,
                             Server::Configuration::ProtocolOptionsFactoryContext&) {
    return absl::InvalidArgumentError(
        fmt::format("filter {} does not support protocol options", name_));
  }

  const std::string name_;
  const bool is_terminal_filter_;
};

/**
 * Common base class for network filter factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto, class ProtocolOptionsProto = ConfigProto>
class FactoryBase : public FactoryCommon<ConfigProto, ProtocolOptionsProto> {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             context);
  }

protected:
  FactoryBase(const std::string& name, bool is_terminal = false)
      : FactoryCommon<ConfigProto, ProtocolOptionsProto>(name, is_terminal) {}

private:
  virtual Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    Server::Configuration::FactoryContext& context) PURE;
};

template <class ConfigProto, class ProtocolOptionsProto = ConfigProto>
class ExceptionFreeFactoryBase : public FactoryCommon<ConfigProto, ProtocolOptionsProto> {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             context);
  }

protected:
  ExceptionFreeFactoryBase(const std::string& name, bool is_terminal = false)
      : FactoryCommon<ConfigProto, ProtocolOptionsProto>(name, is_terminal) {}

private:
  virtual absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    Server::Configuration::FactoryContext& context) PURE;
};

} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
