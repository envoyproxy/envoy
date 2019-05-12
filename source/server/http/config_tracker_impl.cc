#include "server/http/config_tracker_impl.h"

namespace Envoy {
namespace Server {

ConfigTracker::EntryOwnerPtr ConfigTrackerImpl::add(const std::string& key, Cb cb) {
  auto insert_result = map_->emplace(key, std::move(cb));
  return insert_result.second
             ? std::make_unique<ConfigTrackerImpl::EntryOwnerImpl>(map_, std::move(key))
             : nullptr;
}

const ConfigTracker::CbsMap& ConfigTrackerImpl::getCallbacksMap() const { return *map_; }

ConfigTrackerImpl::EntryOwnerImpl::EntryOwnerImpl(const std::shared_ptr<ConfigTracker::CbsMap>& map,
                                                  const std::string& key)
    : map_(map), key_(key) {}

ConfigTrackerImpl::EntryOwnerImpl::~EntryOwnerImpl() {
  size_t erased = map_->erase(key_);
  ASSERT(erased == 1);
}

void ConfigTrackerImpl::addOrUpdateControlPlaneConfig(const std::string& service,
                                                      ControlPlaneConfigPtr control_plane_info) {
  const auto existing_message = control_plane_config_->find(service);
  if (existing_message != control_plane_config_->end()) {
    (*control_plane_config_)[service] = control_plane_info;
  } else {
    control_plane_config_->insert(std::make_pair(service, control_plane_info));
  }
}

ConfigTracker::ControlPlaneConfigPtr
ConfigTrackerImpl::getControlPlaneConfig(const std::string& service) const {
  auto existing_message = control_plane_config_->find(service);
  return existing_message != control_plane_config_->end() ? existing_message->second : nullptr;
}

const ConfigTracker::ControlPlaneConfigMap& ConfigTrackerImpl::getControlPlaneConfigMap() const {
  return *control_plane_config_;
}

} // namespace Server
} // namespace Envoy
