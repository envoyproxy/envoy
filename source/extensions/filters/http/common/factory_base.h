#pragma once

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * Common base class for HTTP filter factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class CommonFactoryBase : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    return std::make_unique<RouteConfigProto>();
  }

  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                  Server::Configuration::ServerFactoryContext& context,
                                  ProtobufMessage::ValidationVisitor& validator) override {
    return createRouteSpecificFilterConfigTyped(
        MessageUtil::downcastAndValidate<const RouteConfigProto&>(proto_config, validator), context,
        validator);
  }

  std::string name() const override { return name_; }

  bool isTerminalFilterByProto(const Protobuf::Message& proto_config,
                               Server::Configuration::ServerFactoryContext& context) override {
    return isTerminalFilterByProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                            proto_config, context.messageValidationVisitor()),
                                        context);
  }

protected:
  CommonFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual bool isTerminalFilterByProtoTyped(const ConfigProto&,
                                            Server::Configuration::ServerFactoryContext&) {
    return false;
  }

  virtual Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfigTyped(const RouteConfigProto&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) {
    return nullptr;
  }

  const std::string name_;
};

/**
 * Specialization for filters which can be upstream or downstream filters.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class FactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto> {
public:
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(
            proto_config, context.getServerFactoryContext().messageValidationVisitor()),
        stats_prefix, context.getServerFactoryContext());
  }

protected:
  FactoryBase(const std::string& name) : CommonFactoryBase<ConfigProto, RouteConfigProto>(name) {}

  virtual Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::ServerFactoryContext& context) PURE;
};

/**
 * Common base class for Downstream-only HTTP filter factories.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class DownstreamFactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto> {
public:
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createDownstreamFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(
            proto_config, context.getServerFactoryContext().messageValidationVisitor()),
        stats_prefix, context);
  }

protected:
  DownstreamFactoryBase(const std::string& name)
      : CommonFactoryBase<ConfigProto, RouteConfigProto>(name) {}
  virtual Http::FilterFactoryCb
  createDownstreamFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                              const std::string& stats_prefix,
                                              Server::Configuration::FactoryContext& context) PURE;
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
