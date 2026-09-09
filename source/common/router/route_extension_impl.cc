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
          "Didn't find a registered route extension implementation for '{}' with type URL '{}'",
          proto_config.name(),
          Envoy::Config::Utility::getFactoryType(proto_config.typed_config())));
    }

    auto typed_config = Envoy::Config::Utility::translateToFactoryConfig(
        proto_config, context.messageValidationVisitor(), *factory);
    auto extension_or_error = factory->createRouteExtension(*typed_config, context);
    RETURN_IF_NOT_OK_REF(extension_or_error.status());

    // A null extension would silently drop a configured extension, so reject it at load.
    if (extension_or_error.value() == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("Route extension '{}' produced a null extension", proto_config.name()));
    }
    extensions.push_back(std::move(extension_or_error.value()));
  }

  return extensions;
}

RouteConstSharedPtr runRouteExtensions(const RouteExtensionList& extensions,
                                       RouteConstSharedPtr route,
                                       const Http::RequestHeaderMap& headers,
                                       const StreamInfo::StreamInfo& stream_info,
                                       uint64_t random_value) {
  for (const RouteExtensionSharedPtr& extension : extensions) {
    route = extension->onRoute(std::move(route), headers, stream_info, random_value);
  }
  return route;
}

absl::Status validateRouteExtensionClusters(const RouteExtensionList& extensions,
                                            const Upstream::ClusterManager& cluster_manager) {
  for (const RouteExtensionSharedPtr& extension : extensions) {
    RETURN_IF_NOT_OK(extension->validateClusters(cluster_manager));
  }
  return absl::OkStatus();
}

} // namespace Router
} // namespace Envoy
