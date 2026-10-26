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

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createHttpFilterRouteConfig(const Protobuf::Message& proto_config,
                              Server::Configuration::ServerFactoryContext& context,
                              Server::Configuration::ExtraFactoryContext& extra_context) override {
    return createHttpFilterRouteConfigTyped(
        MessageUtil::downcastAndValidate<const RouteConfigProto&>(proto_config,
                                                                  extra_context.visitor),
        context, extra_context);
  }

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
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

  virtual absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createHttpFilterRouteConfigTyped(const RouteConfigProto& proto_config,
                                   Server::Configuration::ServerFactoryContext& context,
                                   Server::Configuration::ExtraFactoryContext& extra_context) {
    // Delegate to createRouteSpecificFilterConfigTyped for backwards compatibility.
    return createRouteSpecificFilterConfigTyped(proto_config, context, extra_context.visitor);
  }

  virtual absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const RouteConfigProto&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) {
    return nullptr;
  }

  const std::string name_;
};

/**
 * DEPRECATED: use UnifiedFactoryBase instead. This base class is only kept to give the
 * out-of-tree extensions time to migrate to the unified factory interface and will be removed
 * once the migration is complete.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class [[deprecated(
    "Extend UnifiedFactoryBase and implement createHttpFilterFactoryFromProtoTyped instead")]]
FactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto>,
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

  [[deprecated("Use createHttpFilterFactoryFromProto instead")]]
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContext(
      const Protobuf::Message& proto_config, const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& server_context) override {
    return createFilterFactoryFromProtoWithServerContextTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(
            proto_config, server_context.messageValidationVisitor()),
        stats_prefix, server_context);
  }

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message& proto_config, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    return createHttpFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config,
                                                             context.messageValidationVisitor()),
        context, extra_context);
  }
  virtual absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createHttpFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                        Server::Configuration::ServerFactoryContext& context,
                                        Server::Configuration::ExtraFactoryContext& extra_context) {
    // Delegate to createFilterFactoryFromProtoWithServerContextTyped for backwards compatibility.
    return createFilterFactoryFromProtoWithServerContextTyped(proto_config,
                                                              extra_context.stats_prefix, context);
  }

  [[deprecated("Use createHttpFilterFactoryFromProtoTyped instead")]]
  virtual Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProtoWithServerContextTyped(const ConfigProto&, const std::string&,
                                                     Server::Configuration::ServerFactoryContext&) {
    ExceptionUtil::throwEnvoyException(
        "Creating filter factory from server factory context is not supported");
    return nullptr;
  }
};

/**
 * DEPRECATED: use UnifiedFactoryBase instead. This base class is only kept to give the
 * out-of-tree extensions time to migrate to the unified factory interface and will be removed
 * once the migration is complete.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class [[deprecated(
    "Extend UnifiedFactoryBase and implement createHttpFilterFactoryFromProtoTyped instead")]]
ExceptionFreeFactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto>,
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

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message& proto_config, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    return createHttpFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config,
                                                             context.messageValidationVisitor()),
        context, extra_context);
  }
  virtual absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createHttpFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                        Server::Configuration::ServerFactoryContext& context,
                                        Server::Configuration::ExtraFactoryContext& extra_context) {
    UNREFERENCED_PARAMETER(proto_config);
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(extra_context);
    return absl::InvalidArgumentError(
        "Creating HTTP filter factory from server factory context is not supported");
  }
};

/**
 * DEPRECATED: use UnifiedFactoryBase instead. UnifiedFactoryBase supports both the downstream
 * and the upstream HTTP filter chains. This base class is only kept to give the out-of-tree
 * extensions time to migrate to the unified factory interface and will be removed once the
 * migration is complete.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class [[deprecated(
    "Extend UnifiedFactoryBase and implement createHttpFilterFactoryFromProtoTyped instead")]]
DualFactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto>,
                  public Server::Configuration::NamedHttpFilterConfigFactory,
                  public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  DualFactoryBase(const std::string& name)
      : CommonFactoryBase<ConfigProto, RouteConfigProto>(name) {}

  struct DualInfo {
    DualInfo(Server::Configuration::UpstreamFactoryContext& context)
        : init_manager(context.initManager()), scope(context.scope()), is_upstream(true) {}
    DualInfo(Server::Configuration::FactoryContext& context)
        : init_manager(context.initManager()), scope(context.scope()), is_upstream(false) {}
    Init::Manager& init_manager;
    Stats::Scope& scope;
    bool is_upstream;
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

  // This method is for dual filter to create filter from server context when it is configured
  // in downstream. It won't be called if a dual filter is in upstream.
  [[deprecated("Use createHttpFilterFactoryFromProto instead")]]
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContext(
      const Protobuf::Message& proto_config, const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& server_context) override {
    return createFilterFactoryFromProtoWithServerContextTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(
            proto_config, server_context.messageValidationVisitor()),
        stats_prefix, server_context);
  }

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message& proto_config, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    return createHttpFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config,
                                                             context.messageValidationVisitor()),
        context, extra_context);
  }
  virtual absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createHttpFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                        Server::Configuration::ServerFactoryContext& context,
                                        Server::Configuration::ExtraFactoryContext& extra_context) {
    // Delegate to createFilterFactoryFromProtoWithServerContextTyped for backwards compatibility.
    return createFilterFactoryFromProtoWithServerContextTyped(proto_config,
                                                              extra_context.stats_prefix, context);
  }

private:
  [[deprecated("Use createHttpFilterFactoryFromProtoTyped instead")]]
  virtual Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProtoWithServerContextTyped(const ConfigProto&, const std::string&,
                                                     Server::Configuration::ServerFactoryContext&) {
    ExceptionUtil::throwEnvoyException(
        "DualFactoryBase: creating filter factory from server factory context is not supported");
    return nullptr;
  }
};

/**
 * Base class for HTTP filter factory registrations. This is the recommended base class for all
 * the HTTP filter factories. It supports both the downstream and the upstream HTTP filter chains
 * and only requires the single createHttpFilterFactoryFromProtoTyped() entry point.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class UnifiedFactoryBase : public CommonFactoryBase<ConfigProto, RouteConfigProto>,
                           public Server::Configuration::NamedHttpFilterConfigFactory,
                           public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  UnifiedFactoryBase(const std::string& name)
      : CommonFactoryBase<ConfigProto, RouteConfigProto>(name) {}

  bool isUnifiedFilter() final { return true; }

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) final {
    auto extra_context = Server::Configuration::ExtraFactoryContext::create(context, stats_prefix);
    return createHttpFilterFactoryFromProto(proto_config, context.serverFactoryContext(),
                                            extra_context);
  }

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::UpstreamFactoryContext& context) final {
    auto extra_context = Server::Configuration::ExtraFactoryContext::create(context, stats_prefix);
    return createHttpFilterFactoryFromProto(proto_config, context.serverFactoryContext(),
                                            extra_context);
  }

  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message& proto_config, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) final {
    return createHttpFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config, extra_context.visitor),
        context, extra_context);
  }

  virtual absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const ConfigProto& proto_config, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) PURE;
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
