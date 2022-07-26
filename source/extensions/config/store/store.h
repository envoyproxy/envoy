#pragma once

#include <vector>

#include "envoy/common/key_value_store.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

namespace Envoy {
namespace Extensions {
namespace Config {

inline constexpr char XDS_CONFIG_KEY[] = "xds_config";

class Store {
public:
  explicit Store(KeyValueStore& kv_store);

  std::vector<envoy::service::discovery::v3::Resource>
  getPersistedResources(absl::string_view control_plane_id, absl::string_view resource_type_url);

private:
  KeyValueStore& kv_store_;
};

} // namespace Config
} // namespace Extensions
} // namespace Envoy