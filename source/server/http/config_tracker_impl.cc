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

void ConfigTrackerImpl::addOrUpdateManagedConfig(const std::string& key,
                                                 ProtobufTypes::MessageSharedPtr message) {
  const auto existing_message = managed_config_->find(key);
  if (existing_message != managed_config_->end()) {
    (*managed_config_)[key] = message;
  } else {
    managed_config_->insert(std::make_pair(key, message));
  }
}

ProtobufTypes::MessageSharedPtr ConfigTrackerImpl::getManagedConfig(const std::string& key) const {
  auto existing_message = managed_config_->find(key);
  return existing_message != managed_config_->end() ? existing_message->second : nullptr;
}

const ConfigTracker::ManagedConfigMap& ConfigTrackerImpl::getManagedConfigMap() const {
  return *managed_config_;
}

} // namespace Server
} // namespace Envoy
