#include "source/extensions/config/listeners/config_saver.h"

#include <chrono>

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/config/store/saved_xds_config.pb.h"
#include "source/extensions/config/store/store.h"

namespace Envoy {
namespace Extensions {
namespace Config {

ConfigSaver::ConfigSaver(KeyValueStore& store, TimeSource& time_source)
    : store_(store), time_source_(time_source) {}

void ConfigSaver::onConfigUpdated(const std::string& control_plane_id,
                                  const std::string& resource_type_url,
                                  const std::vector<Envoy::Config::DecodedResourceRef>& resources) {
  // TODO(abeyad): We keep having to reparse the persisted value in order to update it
  // and persist it again; try to find a better way.
  Envoy::Extensions::Config::SavedXdsConfig xds_config;
  if (auto existing_config = store_.get(XDS_CONFIG_KEY)) {
    xds_config.ParseFromString(std::string(*existing_config));
  }

  auto& type_resources = (*xds_config.mutable_per_control_plane_config())[control_plane_id];
  auto& resource_list = (*type_resources.mutable_per_type_resources())[resource_type_url];
  resource_list.clear_resources();
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
  if (resource_list.resources_size() == 0) {
    type_resources.mutable_per_type_resources()->erase(resource_type_url);
    if (type_resources.per_type_resources_size() == 0) {
      xds_config.mutable_per_control_plane_config()->erase(control_plane_id);
    }
  }
  if (xds_config.per_control_plane_config_size() == 0) {
    // There's nothing left in the xDS config, so remove from storage.
    // TODO(abeyad): add some metrics for this.  If we're in this state, it probably means some
    // issues in either the control plane's responses or in Envoy's processing of the resources.
    store_.remove(XDS_CONFIG_KEY);
    return;
  }

  TimestampUtil::systemClockToTimestamp(time_source_.systemTime(),
                                        *xds_config.mutable_last_updated());
  store_.addOrUpdate(XDS_CONFIG_KEY, xds_config.SerializeAsString());
}

} // namespace Config
} // namespace Extensions
} // namespace Envoy
