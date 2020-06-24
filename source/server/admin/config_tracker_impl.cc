#include "server/admin/config_tracker_impl.h"

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

} // namespace Server
} // namespace Envoy
