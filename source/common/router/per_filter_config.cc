#include "source/common/router/per_filter_config.h"

#include "envoy/server/filter_config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Router {

absl::StatusOr<std::unique_ptr<PerFilterConfigs>>
PerFilterConfigs::create(const Protobuf::Map<std::string, Protobuf::Any>& typed_configs,
                         Server::Configuration::ServerFactoryContext& factory_context,
                         ProtobufMessage::ValidationVisitor& validator) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<PerFilterConfigs>(
      new PerFilterConfigs(typed_configs, factory_context, validator, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr>
PerFilterConfigs::createRouteSpecificFilterConfig(
    const std::string& name, const Protobuf::Any& typed_config, bool is_optional,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {
  Server::Configuration::NamedHttpFilterConfigFactory* factory =
      Envoy::Config::Utility::getFactoryByType<Server::Configuration::NamedHttpFilterConfigFactory>(
          typed_config);
  if (factory == nullptr) {
    if (is_optional) {
      ENVOY_LOG(warn,
                "Can't find a registered implementation for http filter '{}' with type URL: '{}'",
                name, Envoy::Config::Utility::getFactoryType(typed_config));
      return nullptr;
    } else {
      return absl::InvalidArgumentError(
          fmt::format("Didn't find a registered implementation for '{}' with type URL: '{}'", name,
                      Envoy::Config::Utility::getFactoryType(typed_config)));
    }
  }

  ProtobufTypes::MessagePtr proto_config = factory->createEmptyRouteConfigProto();
  RETURN_IF_NOT_OK(
      Envoy::Config::Utility::translateOpaqueConfig(typed_config, validator, *proto_config));
  auto object_status_or_error =
      factory->createRouteSpecificFilterConfig(*proto_config, factory_context, validator);
  RETURN_IF_NOT_OK(object_status_or_error.status());
  auto object = std::move(*object_status_or_error);
  if (object == nullptr) {
    if (is_optional) {
      ENVOY_LOG(
          debug,
          "The filter {} doesn't support virtual host or route specific configurations, and it is "
          "optional, so ignore it.",
          name);
    } else {
      return absl::InvalidArgumentError(fmt::format(
          "The filter {} doesn't support virtual host or route specific configurations", name));
    }
  }
  return object;
}

PerFilterConfigs::PerFilterConfigs(const Protobuf::Map<std::string, Protobuf::Any>& typed_configs,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   ProtobufMessage::ValidationVisitor& validator,
                                   absl::Status& creation_status) {

  static const std::string filter_config_type(
      envoy::config::route::v3::FilterConfig::default_instance().GetTypeName());

  for (const auto& per_filter_config : typed_configs) {
    const std::string& name = per_filter_config.first;
    absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr> config_or_error;

    if (TypeUtil::typeUrlToDescriptorFullName(per_filter_config.second.type_url()) ==
        filter_config_type) {
      envoy::config::route::v3::FilterConfig filter_config;
      creation_status = Envoy::Config::Utility::translateOpaqueConfig(per_filter_config.second,
                                                                      validator, filter_config);
      if (!creation_status.ok()) {
        return;
      }

      // The filter is marked as disabled explicitly and the config is ignored directly.
      if (filter_config.disabled()) {
        configs_.emplace(name, FilterConfig{nullptr, true});
        continue;
      }

      // If the field `config` is not configured, we treat it as configuration error.
      if (!filter_config.has_config()) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("Empty route/virtual host per filter configuration for {} filter", name));
        return;
      }

      // If the field `config` is configured but is empty, we treat the filter is enabled
      // explicitly.
      if (filter_config.config().type_url().empty()) {
        configs_.emplace(name, FilterConfig{nullptr, false});
        continue;
      }

      config_or_error = createRouteSpecificFilterConfig(
          name, filter_config.config(), filter_config.is_optional(), factory_context, validator);
    } else {
      config_or_error = createRouteSpecificFilterConfig(name, per_filter_config.second, false,
                                                        factory_context, validator);
    }
    SET_AND_RETURN_IF_NOT_OK(config_or_error.status(), creation_status);

    // If a filter is explicitly configured we treat it as enabled.
    // The config may be nullptr because the filter could be optional.
    configs_.emplace(name, FilterConfig{std::move(config_or_error.value()), false});
  }
}

const RouteSpecificFilterConfig* PerFilterConfigs::get(absl::string_view name) const {
  auto it = configs_.find(name);
  return it == configs_.end() ? nullptr : it->second.config_.get();
}

absl::optional<bool> PerFilterConfigs::disabled(absl::string_view name) const {
  // Quick exit if there are no configs.
  if (configs_.empty()) {
    return absl::nullopt;
  }

  const auto it = configs_.find(name);
  return it != configs_.end() ? absl::optional<bool>{it->second.disabled_} : absl::nullopt;
}

} // namespace Router
} // namespace Envoy
