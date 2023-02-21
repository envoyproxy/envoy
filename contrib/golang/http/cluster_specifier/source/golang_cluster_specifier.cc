#include <chrono>

#include "source/common/router/config_impl.h"

#include "contrib/golang/http/cluster_specifier/source/golang_cluster_specifier.h"

namespace Envoy {
namespace Router {
namespace Golang {

ClusterConfig::ClusterConfig(const GolangClusterProto& config)
    : so_id_(config.library_id()), so_path_(config.library_path()),
      default_cluster_(config.default_cluster()), config_(config.config()) {
  ENVOY_LOG_MISC(debug, "load golang library at parse cluster specifier plugin config: {} {}",
                 so_id_, so_path_);

  // loads DSO store a static map and a open handles leak will occur when the filter gets loaded and
  // unloaded.
  // TODO: unload DSO when filter updated.
  auto res = Envoy::Dso::DsoInstanceManager::load(so_id_, so_path_);
  if (!res) {
    throw EnvoyException(fmt::format("golang_cluster_specifier_plugin: load library failed: {} {}",
                                     so_id_, so_path_));
  }

  dynamic_lib_ = Dso::DsoInstanceManager::getDsoInstanceByID(so_id_);
  if (dynamic_lib_ == nullptr) {
    throw EnvoyException(fmt::format("golang_cluster_specifier_plugin: get library failed: {} {}",
                                     so_id_, so_path_));
  }

  std::string str;
  if (!config_.SerializeToString(&str)) {
    ENVOY_LOG(error, "failed to serialize any pb to string");
    return;
  }

  auto ptr = reinterpret_cast<unsigned long long>(str.data());
  auto len = str.length();
  plugin_id_ = dynamic_lib_->envoyGoClusterSpecifierNewPlugin(ptr, len);
  if (plugin_id_ == 0) {
    ENVOY_LOG(error, "invalid golang plugin config");
  }
}

RouteConstSharedPtr
GolangClusterSpecifierPlugin::route(const RouteEntry& parent,
                                    const Http::RequestHeaderMap& headers) const {
  ASSERT(dynamic_cast<const RouteEntryImplBase*>(&parent) != nullptr);
  int buffer_len = 256;
  std::string buffer;
  std::string cluster;
  auto dlib = config_->getDsoLib();

again:
  buffer.reserve(buffer_len);
  auto plugin_id = config_->getPluginId();
  auto header_ptr = reinterpret_cast<uint64_t>(&headers);
  auto buffer_ptr = reinterpret_cast<uint64_t>(buffer.data());
  auto new_len = dlib != nullptr
                     ? dlib->envoyGoOnClusterSpecify(header_ptr, plugin_id, buffer_ptr, buffer_len)
                     : 0;

  if (new_len == 0) {
    ENVOY_LOG(info, "golang choose the default cluster");
    cluster = config_->defaultCluster();
  } else if (new_len < 0) {
    ENVOY_LOG(error, "error happened while golang choose cluster, using the default cluster");
    cluster = config_->defaultCluster();
  } else if (new_len <= buffer_len) {
    ENVOY_LOG(debug, "buffer size fit the cluster name from golang: {}", new_len);
    cluster = std::string{buffer.data(), size_t(new_len)};
  } else {
    ENVOY_LOG(debug, "need larger size of buffer to save the cluster name in golang, try again");
    buffer_len = new_len;
    goto again;
  }

  // TODO: add cache for it?
  return std::make_shared<RouteEntryImplBase::DynamicRouteEntry>(
      dynamic_cast<const RouteEntryImplBase*>(&parent), cluster);
}

} // namespace Golang
} // namespace Router
} // namespace Envoy
