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

ConfigTrackerImpl::EntryOwnerImpl::EntryOwnerImpl(std::weak_ptr<ConfigTracker::CbsMap> map_weak,
                                                  std::string key)
    : map_weak_(std::move(map_weak)), key_(std::move(key)) {}

ConfigTrackerImpl::EntryOwnerImpl::~EntryOwnerImpl() {
  if (const auto map_strong = map_weak_.lock()) {
    size_t erased = map_strong->erase(key_);
    ASSERT(erased == 1);
  }
}

} // namespace Server
} // namespace Envoy
