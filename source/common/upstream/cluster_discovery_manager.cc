#include "common/upstream/cluster_discovery_manager.h"

#include <functional>

namespace Envoy {
namespace Upstream {

namespace {

using ClusterAddedCb = std::function<void(ThreadLocalCluster&)>;

class ClusterCallbacks : public ClusterUpdateCallbacks {
public:
  ClusterCallbacks(ClusterAddedCb cb) : cb_(std::move(cb)) {}

  void onClusterAddOrUpdate(ThreadLocalCluster& cluster) override { cb_(cluster); };

  void onClusterRemoval(const std::string&) override {}

private:
  ClusterAddedCb cb_;
};

} // namespace

ClusterManagerImpl::ClusterDiscoveryManager::ClusterDiscoveryManager(
    std::string thread_name, ClusterLifecycleCallbackHandler& lifecycle_callbacks_handler)
    : thread_name_(std::move(thread_name)) {
  callbacks_ = std::make_unique<ClusterCallbacks>([this](ThreadLocalCluster& cluster) {
    ENVOY_LOG(trace,
              "cm cdm: starting processing cluster name {} (status {}) from cluster lifecycle "
              "callback in {}",
              cluster.info()->name(), enumToInt(ClusterDiscoveryStatus::Available), thread_name_);
    processClusterName(cluster.info()->name(), ClusterDiscoveryStatus::Available);
  });
  callbacks_handle_ = lifecycle_callbacks_handler.addClusterUpdateCallbacks(*callbacks_);
}

void ClusterManagerImpl::ClusterDiscoveryManager::processClusterName(
    absl::string_view name, ClusterDiscoveryStatus cluster_status) {
  auto callback_items = extractCallbackList(name);
  if (callbacks.empty()) {
    ENVOY_LOG(trace, "cm cdm: no callbacks for the cluster name {} in {}", name, thread_name_);
    return;
  }
  ENVOY_LOG(trace, "cm cdm: invoking {} callbacks for the cluster name {} in {}", callbacks.size(),
            name, thread_name_);
  for (auto& item : callback_items) {
    auto callback = std::move(item->callback);
    // This invalidates the handle and the immediate invoke context.o
    item.reset();
    // The callback could be null when handle was destroyed during the
    // previous callback.
    if (callback != nullptr) {
      (*callback)(cluster_status);
    }
  }
}

ClusterManagerImpl::ClusterDiscoveryManager::AddedCallbackData
ClusterManagerImpl::ClusterDiscoveryManager::addCallback(absl::string_view name,
                                                         ClusterDiscoveryCallbackPtr callback) {
  ENVOY_LOG(trace, "cm cdm: adding callback for the cluster name {} in {}", name, thread_name_);
  auto& callbacks_list = pending_clusters_[name];
  auto item_weak_ptr = addCallbackInternal(callbacks_list, std::move(callback));
  auto handle = std::make_unique<ClusterDiscoveryCallbackHandleImpl>(*this, name, item_weak_ptr);
  InvokeContext invoke_context(*this, name, std::move(item_weak_ptr));
  auto discovery_in_progress = (callbacks_list.size() > 1);
  return {std::move(handle), discovery_in_progress, std::move(invoke_context)};
}

void ClusterManagerImpl::ClusterDiscoveryManager::invokeCallbackFromItem(
    absl::string_view name, CallbackListItemWeakPtr item_weak_ptr,
    ClusterDiscoveryStatus cluster_status) {
  auto item_ptr = item_weak_ptr.lock();
  if (item_ptr == nullptr) {
    ENVOY_LOG(trace, "cm cdm: not invoking an already stale callback for cluster {} in {}", name,
              thread_name_);
    return;
  }
  ENVOY_LOG(trace, "cm cdm: invoking a callback for cluster {} in {}", name, thread_name_);
  auto callback = std::move(item_ptr->callback);
  eraseItem(name, std::move(item_ptr));
  ASSERT(callback != nullptr);
  (*callback)(cluster_status);
}

ClusterManagerImpl::ClusterDiscoveryManager::CallbackList
ClusterManagerImpl::ClusterDiscoveryManager::extractCallbackList(absl::string_view name) {
  auto map_node_handle = pending_clusters_.extract(name);
  if (map_node_handle.empty()) {
    return {};
  }
  CallbackList extracted;
  map_node_handle.mapped().swap(extracted);
  for (auto& item : extracted) {
    item->self_iterator_.reset();
  }
  return extracted;
}

ClusterManagerImpl::ClusterDiscoveryManager::CallbackListItemWeakPtr
ClusterManagerImpl::ClusterDiscoveryManager::addCallbackInternal(
    CallbackList& list, ClusterDiscoveryCallbackPtr callback) {
  auto item = std::make_shared<CallbackListItem>(std::move(callback));
  auto it = list.emplace(list.end(), item);
  item->self_iterator_ = std::move(it);
  return item;
}

void ClusterManagerImpl::ClusterDiscoveryManager::erase(absl::string_view name,
                                                        CallbackListItemWeakPtr item_weak_ptr) {
  auto item_ptr = item_weak_ptr.lock();
  if (item_ptr == nullptr) {
    ENVOY_LOG(trace, "cm cdm: not dropping a stale callback for the cluster name {} in {}", name,
              thread_name_);
    return;
  }
  ENVOY_LOG(trace, "cm cdm: dropping callback for the cluster name {} in {}", name, thread_name_);
  if (!item_ptr->self_iterator.has_value()) {
    ENVOY_LOG(trace,
              "cm cdm: callback for the cluster name {} in {} is not on the callbacks list "
              "anymore, which means it is about to be invoked; preventing it",
              name, thread_name_);
    item_ptr->callback.reset();
    return;
  }
  eraseItem(name, std::move(item_ptr))
}

void ClusterManagerImpl::ClusterDiscoveryManager::eraseItem(absl::string_view name,
                                                            CallbackListItemSharedPtr item_ptr) {
  ASSERT(item_ptr != nullptr);
  ASSERT(item_ptr->self_iterator_.has_value());
  const bool drop_list = eraseFromList(name, item_ptr->self_iterator_);
  item_ptr->self_iterator_.reset();
  if (drop_list) {
    ENVOY_LOG(trace, "cm cdm: dropped last callback for the cluster name {} in {}", name,
              thread_name_);
    pending_clusters_.erase(name);
  }
}

bool ClusterManagerImpl::ClusterDiscoveryManager::eraseFromList(absl::string_view name,
                                                                CallbackListIterator it) {
  auto map_it = pending_clusters_.find(name);
  if (map_it == pending_clusters_.end()) {
    ENVOY_LOG(trace,
              "cm cdm: no callbacks for the cluster name {} found, no callbacks to remove in {}",
              name, thread_name_);
    return false;
  }
  auto& list = map_it->second;
  list.erase(it);
  return list.empty();
}

} // namespace Upstream
} // namespace Envoy
