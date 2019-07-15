#include "common/config/watch_impl.h"

namespace Envoy {
namespace Config {

// The return value logic:
// 1) if update_to_these_names contains X, and no other Watch in the parent WatchMap
//    cares about X, then X will be in added_.
// 2) if update_to_these_names does not contain Y, and this Watch was the only one in the
//    WatchMap that cared about Y, then Y will be in removed_.
AddedRemoved WatchImpl::updateWatchInterest(const std::set<std::string>& update_to_these_names) {
  parent_map_.setWildcardness(this, update_to_these_names.empty());

  std::vector<std::string> newly_added_to_watch;
  std::set_difference(update_to_these_names.begin(), update_to_these_names.end(),
                      resource_names_.begin(), resource_names_.end(),
                      std::inserter(newly_added_to_watch, newly_added_to_watch.begin()));

  std::vector<std::string> newly_removed_from_watch;
  std::set_difference(resource_names_.begin(), resource_names_.end(), update_to_these_names.begin(),
                      update_to_these_names.end(),
                      std::inserter(newly_removed_from_watch, newly_removed_from_watch.begin()));

  resource_names_ = update_to_these_names;

  return AddedRemoved(parent_map_.findAdditions(newly_added_to_watch, this),
                      parent_map_.findRemovals(newly_removed_from_watch, this));
}

void WatchImpl::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                               const std::string& version_info) {
  callbacks_.onConfigUpdate(resources, version_info);
}

void WatchImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
}

void WatchImpl::onConfigUpdateFailed(const EnvoyException* e) {
  callbacks_.onConfigUpdateFailed(e);
}

std::string WatchImpl::resourceName(const ProtobufWkt::Any& resource) {
  return callbacks_.resourceName(resource);
}

} // namespace Config
} // namespace Envoy
