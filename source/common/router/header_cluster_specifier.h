#pragma once

#include "envoy/router/cluster_specifier_plugin.h"

namespace Envoy {
namespace Router {

class HeaderClusterSpecifierPlugin : public ClusterSpecifierPlugin {
public:
  HeaderClusterSpecifierPlugin(absl::string_view cluster_header);

  RouteConstSharedPtr route(RouteEntryAndRouteConstSharedPtr parent,
                            const Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info,
                            uint64_t random) const override;

private:
  const Http::LowerCaseString cluster_header_;
};

} // namespace Router
} // namespace Envoy
