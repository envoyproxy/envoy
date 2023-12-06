#pragma once

#include "envoy/http/filter_factory.h"
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
class CommonFactoryBase : public virtual Server::Configuration::HttpFilterConfigFactoryBase {
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

  virtual bool isTerminalFilterByProtoTyped(const ConfigProto&,
                                            Server::Configuration::ServerFactoryContext&) {
    return false;
  }

protected:
  CommonFactoryBase(const std::string& name) : name_(name) {}

  virtual Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfigTyped(const RouteConfigProto&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) {
    return nullptr;
  }

  const std::string name_;
};

template <class ConfigProto, class RouteConfigProto = ConfigProto>
class FactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto>,
                    public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  FactoryBase(const std::string& name) : CommonFactoryBase<ConfigProto, RouteConfigProto>(name) {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             stats_prefix, context);
  }
  virtual Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;

  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContext(
      const Protobuf::Message& proto_config, const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& server_context) override {
    return createFilterFactoryFromProtoWithServerContextTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(
            proto_config, server_context.messageValidationVisitor()),
        stats_prefix, server_context);
  }

  virtual Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProtoWithServerContextTyped(const ConfigProto&, const std::string&,
                                                     Server::Configuration::ServerFactoryContext&) {
    ExceptionUtil::throwEnvoyException(
        "Creating filter factory from server factory context is not supported");
    return nullptr;
  }
};

template <class ConfigProto, class RouteConfigProto = ConfigProto>
class ExceptionFreeFactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto>,
                                 public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  ExceptionFreeFactoryBase(const std::string& name)
      : CommonFactoryBase<ConfigProto, RouteConfigProto>(name) {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             stats_prefix, context);
  }
  virtual absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;
};

template <class ConfigProto, class RouteConfigProto = ConfigProto>
class DualFactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto>,
                        public Server::Configuration::NamedHttpFilterConfigFactory,
                        public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  DualFactoryBase(const std::string& name)
      : CommonFactoryBase<ConfigProto, RouteConfigProto>(name) {}

  struct DualInfo {
    DualInfo(Server::Configuration::UpstreamFactoryContext& context)
        : init_manager(context.initManager()), scope(context.scope()) {}
    DualInfo(Server::Configuration::FactoryContext& context)
        : init_manager(context.initManager()), scope(context.scope()) {}
    Init::Manager& init_manager;
    Stats::Scope& scope;
  };

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             stats_prefix, DualInfo(context),
                                             context.serverFactoryContext());
  }

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::UpstreamFactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(
            proto_config, context.serverFactoryContext().messageValidationVisitor()),
        stats_prefix, DualInfo(context), context.serverFactoryContext());
  }

  virtual absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    const std::string& stats_prefix, DualInfo info,
                                    Server::Configuration::ServerFactoryContext& context) PURE;
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
