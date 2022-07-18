#pragma once

#include "envoy/common/key_value_store.h"
#include "envoy/config/config_updated_listener.h"
#include "envoy/config/subscription.h"

namespace Envoy {
namespace Config {

class ConfigSaver : public ConfigUpdatedListener {
public:
  ConfigSaver(KeyValueStore& store);

  void onConfigUpdated(const std::string& control_plane_id, const std::string& resource_type_url,
                       const std::vector<DecodedResourceRef>& resources) override;

private:
  KeyValueStore& store_;
};

} // namespace Config
} // namespace Envoy