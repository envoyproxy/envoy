#include "source/common/router/header_cluster_specifier.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {
namespace {

std::string extractClusterFromHeader(const Http::RequestHeaderMap& headers,
                                     const Http::LowerCaseString& cluster_header) {
  const auto entry = headers.get(cluster_header);
  if (!entry.empty()) {
    // This is an implicitly untrusted header, so per the API documentation only
    // the first value is used.
    return std::string(entry[0]->value().getStringView());
  }
  return std::string{};
}

class HeaderClusterRouteEntry : public DynamicRouteEntry {
public:
  HeaderClusterRouteEntry(RouteConstSharedPtr route, std::string&& cluster_name,
                          const Http::LowerCaseString& cluster_header)
      : DynamicRouteEntry(std::move(route), std::move(cluster_name)),
        cluster_header_(cluster_header) {}

  void refreshRouteCluster(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo&) const override {
    // HeaderClusterRouteEntry is instantiated per request and accessed only on a single worker
    // thread, matching WeightedClusterEntry::refreshRouteCluster.
    const_cast<HeaderClusterRouteEntry*>(this)->cluster_name_ =
        extractClusterFromHeader(headers, cluster_header_);
  }

private:
  const Http::LowerCaseString cluster_header_;
};

} // namespace

HeaderClusterSpecifierPlugin::HeaderClusterSpecifierPlugin(absl::string_view cluster_header)
    : cluster_header_(cluster_header) {}

RouteConstSharedPtr HeaderClusterSpecifierPlugin::route(RouteEntryAndRouteConstSharedPtr parent,
                                                        const Http::RequestHeaderMap& headers,
                                                        const StreamInfo::StreamInfo&,
                                                        uint64_t) const {
  return std::make_shared<HeaderClusterRouteEntry>(
      std::move(parent), extractClusterFromHeader(headers, cluster_header_), cluster_header_);
}

} // namespace Router
} // namespace Envoy
