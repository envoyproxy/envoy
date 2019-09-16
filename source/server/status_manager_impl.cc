#include "server/status_manager_impl.h"

namespace Envoy {
namespace Server {

void StatusManagerImpl::addComponent(absl::string_view component) {
  absl::WriterMutexLock lock(&status_map_lock_);
  status_map_[component] = nullptr;
}

void StatusManagerImpl::updateStatus(absl::string_view component,
                                     std::unique_ptr<StatusHandle>&& status) {
  absl::WriterMutexLock lock(&status_map_lock_);
  auto it = status_map_.find(component);
  it->second = std::move(status);
}

StatusManager::StatusHandle StatusManagerImpl::status(absl::string_view component) {
  absl::ReaderMutexLock lock(&status_map_lock_);
  StatusHandle ret = *status_map_.find(component)->second;
  return ret;
}

} // namespace Server
} // namespace Envoy
