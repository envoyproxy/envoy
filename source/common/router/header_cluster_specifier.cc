#include "source/common/router/header_cluster_specifier.h"

#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {

HeaderClusterSpecifierPlugin::HeaderClusterSpecifierPlugin(absl::string_view cluster_header)
    : cluster_header_(cluster_header) {}

RouteConstSharedPtr HeaderClusterSpecifierPlugin::route(RouteEntryAndRouteConstSharedPtr parent,
                                                        const Http::RequestHeaderMap& headers,
                                                        const StreamInfo::StreamInfo&) const {

  const auto entry = headers.get(cluster_header_);
  absl::string_view cluster_name;
  if (!entry.empty()) {
    // This is an implicitly untrusted header, so per the API documentation only
    // the first value is used.
    cluster_name = entry[0]->value().getStringView();
  }
  return std::make_shared<DynamicRouteEntry>(std::move(parent), std::string(cluster_name));
}

} // namespace Router
} // namespace Envoy
