#pragma once

#include "envoy/server/filter_config.h"

#include "common/common/fmt.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * Common base class for HTTP filter factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class FactoryBase : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             stats_prefix, context);
  }

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

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;

  virtual Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfigTyped(const RouteConfigProto&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) {
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.check_unsupported_typed_per_filter_config")) {
      throw EnvoyException(fmt::format(
          "The filter {} doesn't support virtual host-specific configurations", name()));
    }
    ENVOY_LOG(warn,
              "The filter {} doesn't support virtual host-specific configurations. Set runtime "
              "config `envoy.reloadable_features.check_unsupported_typed_per_filter_config` as "
              "true to reject any invalid virtual-host specific configuration.",
              name())
    return nullptr;
  }

  const std::string name_;
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
