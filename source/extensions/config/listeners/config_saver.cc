#include "source/extensions/config/listeners/config_saver.h"

#include <chrono>

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/extensions/config/store/saved_xds_config.pb.h"
#include "source/extensions/config/store/store.h"

namespace Envoy {
namespace Extensions {
namespace Config {

ConfigSaver::ConfigSaver(KeyValueStore& store) : store_(store) {}

void ConfigSaver::onConfigUpdated(const std::string& control_plane_id,
                                  const std::string& resource_type_url,
                                  const std::vector<Envoy::Config::DecodedResourceRef>& resources) {
  // TODO(abeyad): this sucks, we keep having to reparse the persisted value in order to update it
  // and persist it again.  Find a better way.
  Envoy::Extensions::Config::SavedXdsConfig xds_config;
  if (auto existing_config = store_.get(XDS_CONFIG_KEY)) {
    xds_config.ParseFromString(std::string(*existing_config));
  }
  auto& type_resources = (*xds_config.mutable_per_server_config())[control_plane_id];
  auto& resource_list = (*type_resources.mutable_per_type_resources())[resource_type_url];
  for (const auto& resource_ref : resources) {
    const auto& decoded_resource = resource_ref.get();
    if (decoded_resource.hasResource()) {
      envoy::service::discovery::v3::Resource r;
      // TODO(abeyad): Support dynamic parameter constraints.
      r.set_name(decoded_resource.name());
      r.set_version(decoded_resource.version());
      r.mutable_resource()->PackFrom(decoded_resource.resource());
      if (decoded_resource.ttl()) {
        r.mutable_ttl()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(
            decoded_resource.ttl().value().count()));
      }
      *resource_list.add_resources() = std::move(r);
    }
  }
  const std::string value = xds_config.SerializeAsString();
  if (value.empty()) {
    store_.remove(XDS_CONFIG_KEY);
  } else {
    store_.addOrUpdate(XDS_CONFIG_KEY, value);
  }
}

} // namespace Config
} // namespace Extensions
} // namespace Envoy
