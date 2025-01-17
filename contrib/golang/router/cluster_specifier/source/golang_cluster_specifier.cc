#include "contrib/golang/router/cluster_specifier/source/golang_cluster_specifier.h"

#include <chrono>

#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Router {
namespace Golang {

// limit the max length of cluster name that could return from the Golang cluster specifier plugin,
// to avoid memory security vulnerability since there might be a bug in Golang side.
#define MAX_CLUSTER_LENGTH 8192

ClusterConfig::ClusterConfig(const GolangClusterProto& config)
    : so_id_(config.library_id()), so_path_(config.library_path()),
      default_cluster_(config.default_cluster()), config_(config.config()) {
  ENVOY_LOG_MISC(debug, "load golang library at parse cluster specifier plugin config: {} {}",
                 so_id_, so_path_);

  // loads DSO store a static map and a open handles leak will occur when the filter gets loaded and
  // unloaded.
  // TODO: unload DSO when filter updated.
  dynamic_lib_ = Envoy::Dso::DsoManager<Dso::ClusterSpecifierDsoImpl>::load(so_id_, so_path_);
  if (dynamic_lib_ == nullptr) {
    throw EnvoyException(fmt::format("golang_cluster_specifier_plugin: load library failed: {} {}",
                                     so_id_, so_path_));
  }

  std::string str;
  if (!config_.SerializeToString(&str)) {
    throw EnvoyException(
        fmt::format("golang_cluster_specifier_plugin: serialize config to string failed: {} {}",
                    so_id_, so_path_));
  }

  auto ptr = reinterpret_cast<unsigned long long>(str.data());
  auto len = str.length();
  plugin_id_ = dynamic_lib_->envoyGoClusterSpecifierNewPlugin(ptr, len);
  if (plugin_id_ == 0) {
    throw EnvoyException(
        fmt::format("golang_cluster_specifier_plugin: generate plugin failed in golang side: {} {}",
                    so_id_, so_path_));
  }
}

RouteConstSharedPtr
GolangClusterSpecifierPlugin::route(RouteConstSharedPtr parent,
                                    const Http::RequestHeaderMap& header) const {
  ASSERT(dynamic_cast<const RouteEntryImplBase*>(parent.get()) != nullptr);
  int buffer_len = 256;
  std::string buffer;
  std::string cluster;
  auto dlib = config_->getDsoLib();
  ASSERT(dlib != nullptr);

  while (true) {
    buffer.reserve(buffer_len);
    auto plugin_id = config_->getPluginId();
    auto header_ptr = reinterpret_cast<uint64_t>(&header);
    auto plugin_ptr = reinterpret_cast<uint64_t>(this);
    auto buffer_ptr = reinterpret_cast<uint64_t>(buffer.data());
    auto new_len =
        dlib->envoyGoOnClusterSpecify(plugin_ptr, header_ptr, plugin_id, buffer_ptr, buffer_len);

    if (new_len <= 0) {
      ENVOY_LOG(debug, "golang cluster specifier choose the default cluster");
      cluster = config_->defaultCluster();
      break;
    } else if (new_len <= buffer_len) {
      ENVOY_LOG(debug, "buffer size fit the cluster name from golang");
      cluster = std::string{buffer.data(), size_t(new_len)};
      break;
    } else {
      RELEASE_ASSERT(new_len <= MAX_CLUSTER_LENGTH, "cluster name too long");
      ENVOY_LOG(debug, "need larger size of buffer to save the cluster name in golang, try again");
      buffer_len = new_len;
    }
  }

  return std::make_shared<RouteEntryImplBase::DynamicRouteEntry>(
      dynamic_cast<const RouteEntryImplBase*>(parent.get()), parent, cluster);
}

void GolangClusterSpecifierPlugin::log(absl::string_view& msg) const {
  ENVOY_LOG(error, "{}", msg);
}

} // namespace Golang
} // namespace Router
} // namespace Envoy
