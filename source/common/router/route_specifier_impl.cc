#include "source/common/router/route_specifier_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
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

OnRouteResult runRouteSpecifiers(RouteSpecifierSpan specifiers, RouteConstSharedPtr route,
                                 const Http::RequestHeaderMap& headers,
                                 const StreamInfo::StreamInfo& stream_info, uint64_t random) {
  OnRouteResult result{std::move(route)};
  for (const auto& specifier : specifiers) {
    result = specifier->onRoute(std::move(result.route), headers, stream_info, random);
    // A specifier that ends the chain, or that drops the route to carry on with matching, is the
    // last one to run, since nothing after it can act on a route that is being left behind.
    if (result.status == OnRouteResultStatus::StopIteration || result.continue_matching) {
      return result;
    }
  }
  return result;
}

RouteConstSharedPtr
applyRouteSpecifiers(RouteConstSharedPtr route, RouteSpecifierSpan config_specifiers,
                     RouteSpecifierSpan vhost_specifiers, const Http::RequestHeaderMap& headers,
                     const StreamInfo::StreamInfo& stream_info, uint64_t random) {
  const auto run_outer = [&](RouteSpecifierSpan specifiers,
                             RouteConstSharedPtr input) -> OnRouteResult {
    OnRouteResult result =
        runRouteSpecifiers(specifiers, std::move(input), headers, stream_info, random);
    if (result.continue_matching) {
      ENVOY_LOG_MISC(debug, "continue_matching from an outer route specifier chain is ignored");
    }
    return result;
  };

  if (!config_specifiers.empty()) {
    OnRouteResult result = run_outer(config_specifiers, std::move(route));
    route = std::move(result.route);
    if (result.status == OnRouteResultStatus::StopIteration) {
      return route;
    }
  }

  if (!vhost_specifiers.empty()) {
    OnRouteResult result = run_outer(vhost_specifiers, std::move(route));
    route = std::move(result.route);
    if (result.status == OnRouteResultStatus::StopIteration) {
      return route;
    }
  }

  return route;
}

} // namespace Router
} // namespace Envoy
