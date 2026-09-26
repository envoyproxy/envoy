#include "source/common/router/route_specifier_impl.h"

#include "envoy/common/exception.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Router {

absl::StatusOr<RouteSpecifierList> createRouteSpecifiers(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& configs,
    RouteSpecifierFactoryContext& context) {
  RouteSpecifierList specifiers;
  specifiers.reserve(configs.size());

  for (const auto& proto_config : configs) {
    auto* factory = Envoy::Config::Utility::getFactory<RouteSpecifierFactory>(proto_config);
    if (factory == nullptr) {
      return absl::InvalidArgumentError(fmt::format(
          "Didn't find a registered route specifier implementation for '{}' with type URL: '{}'",
          proto_config.name(),
          Envoy::Config::Utility::getFactoryType(proto_config.typed_config())));
    }

    auto typed_config = Envoy::Config::Utility::translateToFactoryConfig(
        proto_config, context.serverFactoryContext().messageValidationVisitor(), *factory);
    auto specifier_or_error = factory->createRouteSpecifier(*typed_config, context);
    RETURN_IF_NOT_OK_REF(specifier_or_error.status());

    // A factory returning nullptr would silently drop the specifier, which is never what the
    // configuration asked for.
    if (specifier_or_error.value() == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("Route specifier '{}' produced a null specifier", proto_config.name()));
    }
    specifiers.push_back(std::move(specifier_or_error.value()));
  }

  return specifiers;
}

namespace {

// Run a single level of the chain, stopping at the first specifier that declares its result final.
// A nullptr route is not a stop condition: it is passed on to the next specifier, which is free to
// supply one.
OnRouteResult runSpecifiers(RouteSpecifierSpan specifiers, RouteConstSharedPtr route,
                            const Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info, uint64_t random) {
  for (const auto& specifier : specifiers) {
    auto result = specifier->onRoute(std::move(route), headers, stream_info, random);
    if (result.status == OnRouteResultStatus::StopIteration) {
      return result;
    }
    route = std::move(result.route);
  }
  return {std::move(route), OnRouteResultStatus::Continue};
}

} // namespace

RouteConstSharedPtr
applyRouteSpecifiers(RouteConstSharedPtr route, RouteSpecifierSpan config_specifiers,
                     RouteSpecifierSpan vhost_specifiers, RouteSpecifierSpan route_specifiers,
                     const Http::RequestHeaderMap& headers,
                     const StreamInfo::StreamInfo& stream_info, uint64_t random) {
  if (!config_specifiers.empty()) {
    auto result = runSpecifiers(config_specifiers, std::move(route), headers, stream_info, random);
    route = std::move(result.route);
    if (result.status == OnRouteResultStatus::StopIteration) {
      return route;
    }
  }

  if (!vhost_specifiers.empty()) {
    auto result = runSpecifiers(vhost_specifiers, std::move(route), headers, stream_info, random);
    route = std::move(result.route);
    if (result.status == OnRouteResultStatus::StopIteration) {
      return route;
    }
  }

  if (!route_specifiers.empty()) {
    auto result = runSpecifiers(route_specifiers, std::move(route), headers, stream_info, random);
    route = std::move(result.route);
    if (result.status == OnRouteResultStatus::StopIteration) {
      return route;
    }
  }

  return route;
}

} // namespace Router
} // namespace Envoy
