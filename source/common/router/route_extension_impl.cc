#include "source/common/router/route_extension_impl.h"

#include "envoy/common/exception.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Router {

absl::StatusOr<RouteExtensionList> createRouteExtensions(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig>& configs,
    Server::Configuration::ServerFactoryContext& context) {
  RouteExtensionList extensions;
  extensions.reserve(configs.size());

  for (const auto& proto_config : configs) {
    auto* factory = Envoy::Config::Utility::getFactory<RouteExtensionFactory>(proto_config);
    if (factory == nullptr) {
      return absl::InvalidArgumentError(fmt::format(
          "Didn't find a registered route extension implementation for '{}' with type URL: '{}'",
          proto_config.name(),
          Envoy::Config::Utility::getFactoryType(proto_config.typed_config())));
    }

    auto typed_config = Envoy::Config::Utility::translateToFactoryConfig(
        proto_config, context.messageValidationVisitor(), *factory);
    auto extension_or_error = factory->createRouteExtension(*typed_config, context);
    RETURN_IF_NOT_OK_REF(extension_or_error.status());

    // A factory returning nullptr would silently drop the extension, which is never what the
    // configuration asked for.
    if (extension_or_error.value() == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("Route extension '{}' produced a null extension", proto_config.name()));
    }
    extensions.push_back(std::move(extension_or_error.value()));
  }

  return extensions;
}

namespace {

// Run a single level of the chain, stopping at the first extension that declares its result final.
// A nullptr route is not a stop condition: it is passed on to the next extension, which is free to
// supply one.
OnRouteResult runRouteExtensions(RouteExtensionSpan extensions, RouteConstSharedPtr route,
                                 const Http::RequestHeaderMap& headers,
                                 const StreamInfo::StreamInfo& stream_info, uint64_t random_value) {
  for (const RouteExtensionSharedPtr& extension : extensions) {
    OnRouteResult result = extension->onRoute(std::move(route), headers, stream_info, random_value);
    if (result.status == OnRouteResultStatus::StopIteration) {
      return result;
    }
    route = std::move(result.route);
  }
  return {std::move(route), OnRouteResultStatus::Continue};
}

} // namespace

RouteConstSharedPtr
applyRouteExtensions(RouteConstSharedPtr route, RouteExtensionSpan config_extensions,
                     RouteExtensionSpan vhost_extensions, RouteExtensionSpan route_extensions,
                     const Http::RequestHeaderMap& headers,
                     const StreamInfo::StreamInfo& stream_info, uint64_t random_value) {
  if (config_extensions.empty() && vhost_extensions.empty() && route_extensions.empty()) {
    return route;
  }

  // StopIteration ends the whole chain, not just the level that raised it, so the later levels are
  // skipped as well.
  OnRouteResult result =
      runRouteExtensions(config_extensions, std::move(route), headers, stream_info, random_value);
  if (result.status == OnRouteResultStatus::Continue) {
    result = runRouteExtensions(vhost_extensions, std::move(result.route), headers, stream_info,
                                random_value);
  }
  if (result.status == OnRouteResultStatus::Continue) {
    result = runRouteExtensions(route_extensions, std::move(result.route), headers, stream_info,
                                random_value);
  }

  return std::move(result.route);
}

} // namespace Router
} // namespace Envoy
