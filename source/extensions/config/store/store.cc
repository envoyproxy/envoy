#include "source/extensions/config/store/store.h"

#include "source/extensions/config/store/saved_xds_config.pb.h"

namespace Envoy {
namespace Extensions {
namespace Config {

Store::Store(KeyValueStore& kv_store) : kv_store_(kv_store) {}

std::vector<envoy::service::discovery::v3::Resource>
Store::getPersistedResources(absl::string_view control_plane_id,
                             absl::string_view resource_type_url) {
  Envoy::Extensions::Config::SavedXdsConfig xds_config;
  if (auto existing_config = kv_store_.get(XDS_CONFIG_KEY)) {
    xds_config.ParseFromString(std::string(*existing_config));
    auto server_cfg = xds_config.per_control_plane_config().find(std::string(control_plane_id));
    if (server_cfg != xds_config.per_control_plane_config().end()) {
      auto resources_list =
          server_cfg->second.per_type_resources().find(std::string(resource_type_url));
      if (resources_list != server_cfg->second.per_type_resources().end()) {
        std::vector<envoy::service::discovery::v3::Resource> resources;
        resources.reserve(resources_list->second.resources_size());
        for (const auto& resource : resources_list->second.resources()) {
          resources.push_back(resource);
        }
        return resources;
      }
    }
  }
  return {};
}

} // namespace Config
} // namespace Extensions
} // namespace Envoy