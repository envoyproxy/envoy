#pragma once

#include "envoy/router/cluster_specifier_plugin.h"

#include "source/common/http/utility.h"

#include "contrib/envoy/extensions/router/cluster_specifier/golang/v3alpha/golang.pb.h"
#include "contrib/golang/common/dso/dso.h"

namespace Envoy {
namespace Router {
namespace Golang {

using GolangClusterProto = envoy::extensions::router::cluster_specifier::golang::v3alpha::Config;

class ClusterConfig : Logger::Loggable<Logger::Id::golang> {
public:
  ClusterConfig(const GolangClusterProto& config);
  uint64_t getPluginId() { return plugin_id_; };
  const std::string& defaultCluster() { return default_cluster_; }
  Dso::ClusterSpecifierDsoPtr getDsoLib() { return dynamic_lib_; }

private:
  const std::string so_id_;
  const std::string so_path_;
  const std::string default_cluster_;
  const Protobuf::Any config_;
  uint64_t plugin_id_{0};
  Dso::ClusterSpecifierDsoPtr dynamic_lib_;
};

using ClusterConfigSharedPtr = std::shared_ptr<ClusterConfig>;

class GolangClusterSpecifierPlugin : public ClusterSpecifierPlugin,
                                     Logger::Loggable<Logger::Id::golang> {
public:
  GolangClusterSpecifierPlugin(ClusterConfigSharedPtr config) : config_(config) {};

  RouteConstSharedPtr route(RouteEntryAndRouteConstSharedPtr parent,
                            const Http::RequestHeaderMap& header,
                            const StreamInfo::StreamInfo& stream_info,
                            uint64_t random) const override;

  void log(absl::string_view& msg) const;

private:
  ClusterConfigSharedPtr config_;
};

} // namespace Golang
} // namespace Router
} // namespace Envoy
