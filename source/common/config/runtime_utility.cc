#include "common/config/runtime_utility.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

namespace Envoy {
namespace Config {

void translateRuntime(const envoy::config::bootstrap::v3::Runtime& runtime_config,
                      envoy::config::bootstrap::v3::LayeredRuntime& layered_runtime_config) {
  {
    auto* layer = layered_runtime_config.add_layers();
    layer->set_name("base");
    layer->mutable_static_layer()->MergeFrom(runtime_config.base());
  }
  if (!runtime_config.symlink_root().empty()) {
    {
      auto* layer = layered_runtime_config.add_layers();
      layer->set_name("root");
      layer->mutable_disk_layer()->set_symlink_root(runtime_config.symlink_root());
      layer->mutable_disk_layer()->set_subdirectory(runtime_config.subdirectory());
    }
    if (!runtime_config.override_subdirectory().empty()) {
      auto* layer = layered_runtime_config.add_layers();
      layer->set_name("override");
      layer->mutable_disk_layer()->set_symlink_root(runtime_config.symlink_root());
      layer->mutable_disk_layer()->set_subdirectory(runtime_config.override_subdirectory());
      layer->mutable_disk_layer()->set_append_service_cluster(true);
    }
  }
  {
    auto* layer = layered_runtime_config.add_layers();
    layer->set_name("admin");
    layer->mutable_admin_layer();
  }
}

} // namespace Config
} // namespace Envoy
