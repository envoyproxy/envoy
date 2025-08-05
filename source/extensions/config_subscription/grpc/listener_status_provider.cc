#include "source/extensions/config_subscription/grpc/listener_status_provider.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

// ListenerStatusProvider implementation
std::vector<Protobuf::Any> ListenerStatusProvider::getResources(
    const std::vector<std::string>& resource_names) const {
  
  absl::ReaderMutexLock lock(&mutex_);
  
  std::vector<Protobuf::Any> resources;
  
  if (resource_names.empty()) {
    // Return all listeners
    resources.reserve(listeners_.size());
    for (const auto& [name, status] : listeners_) {
      Protobuf::Any any;
      any.PackFrom(status);
      resources.push_back(any);
    }
  } else {
    // Return specific listeners
    resources.reserve(resource_names.size());
    for (const auto& name : resource_names) {
      auto it = listeners_.find(name);
      if (it != listeners_.end()) {
        Protobuf::Any any;
        any.PackFrom(it->second);
        resources.push_back(any);
      }
    }
  }
  
  ENVOY_LOG(debug, "Returning {} listener status resources", resources.size());
  return resources;
}

std::string ListenerStatusProvider::getVersionInfo() const {
  absl::ReaderMutexLock lock(&version_mutex_);
  return version_info_;
}

void ListenerStatusProvider::updateListenerStatus(const std::string& name, 
                                                  const envoy::admin::v3::ListenerReadinessStatus& status) {
  ENVOY_LOG(debug, "Updating listener status: {} -> {}", name, 
            envoy::admin::v3::ListenerReadinessStatus::State_Name(status.state()));
  
  {
    absl::WriterMutexLock lock(&mutex_);
    listeners_[name] = status;
  }
  
  incrementVersion();
}

void ListenerStatusProvider::removeListener(const std::string& name) {
  ENVOY_LOG(debug, "Removing listener status: {}", name);
  
  {
    absl::WriterMutexLock lock(&mutex_);
    listeners_.erase(name);
  }
  
  incrementVersion();
}

void ListenerStatusProvider::onListenerAdded(const std::string& name) {
  auto status = createStatus(name, envoy::admin::v3::ListenerReadinessStatus::PENDING);
  updateListenerStatus(name, status);
}

void ListenerStatusProvider::onListenerReady(const std::string& name, const std::string& bound_address) {
  auto status = createStatus(name, envoy::admin::v3::ListenerReadinessStatus::READY, bound_address);
  status.set_ready(true);
  updateListenerStatus(name, status);
}

void ListenerStatusProvider::onListenerFailed(const std::string& name, const std::string& error) {
  auto status = createStatus(name, envoy::admin::v3::ListenerReadinessStatus::FAILED, "", error);
  status.set_ready(false);
  updateListenerStatus(name, status);
}

void ListenerStatusProvider::onListenerDraining(const std::string& name) {
  auto status = createStatus(name, envoy::admin::v3::ListenerReadinessStatus::DRAINING);
  // Keep previous ready state during draining
  {
    absl::ReaderMutexLock lock(&mutex_);
    auto it = listeners_.find(name);
    if (it != listeners_.end()) {
      status.set_ready(it->second.ready());
      status.set_bound_address(it->second.bound_address());
    }
  }
  updateListenerStatus(name, status);
}

envoy::admin::v3::ListenerReadinessStatus ListenerStatusProvider::createStatus(
    const std::string& name,
    envoy::admin::v3::ListenerReadinessStatus::State state,
    const std::string& bound_address,
    const std::string& error_message) const {
  
  envoy::admin::v3::ListenerReadinessStatus status;
  status.set_listener_name(name);
  status.set_state(state);
  status.set_bound_address(bound_address);
  status.set_error_message(error_message);
  
  // Set timestamp
  auto timestamp = Protobuf::util::TimeUtil::GetCurrentTime();
  *status.mutable_last_updated() = timestamp;
  
  return status;
}

void ListenerStatusProvider::incrementVersion() {
  absl::WriterMutexLock lock(&version_mutex_);
  version_counter_++;
  version_info_ = absl::StrCat(version_counter_);
}

} // namespace Server
} // namespace Envoy 