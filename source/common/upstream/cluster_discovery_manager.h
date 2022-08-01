#pragma once

#include <list>
#include <memory>
#include <string>
#include <utility>

#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

/**
 * A base class for cluster lifecycle handler. Mostly to avoid a dependency on
 * ThreadLocalClusterManagerImpl in ClusterDiscoveryManager.
 */
class ClusterLifecycleCallbackHandler {
public:
  virtual ~ClusterLifecycleCallbackHandler() = default;

  virtual ClusterUpdateCallbacksHandlePtr
  addClusterUpdateCallbacks(ClusterUpdateCallbacks& cb) PURE;
};

/**
 * A thread-local on-demand cluster discovery manager. It takes care of invoking the discovery
 * callbacks in the event of a finished discovery. It does it by installing a cluster lifecycle
 * callback that invokes the discovery callbacks when a matching cluster just got added.
 *
 * The manager is the sole owner of the added discovery callbacks. The only way to remove the
 * callback from the manager is by destroying the discovery handle.
 */
class ClusterDiscoveryManager : Logger::Loggable<Logger::Id::upstream> {
private:
  struct CallbackListItem;
  using CallbackListItemSharedPtr = std::shared_ptr<CallbackListItem>;
  using CallbackListItemWeakPtr = std::weak_ptr<CallbackListItem>;
  using CallbackList = std::list<CallbackListItemSharedPtr>;
  using CallbackListIterator = CallbackList::iterator;

public:
  /**
   * This class is used in a case when the cluster manager in the main thread notices that it
   * already has the requested cluster, so instead of starting the discovery process, it schedules
   * the invocation of the callback back to the thread that made the request. Invoking the request
   * removes it from the manager.
   */
  class CallbackInvoker {
  public:
    void invokeCallback(ClusterDiscoveryStatus cluster_status) const {
      parent_.invokeCallbackFromItem(name_, item_weak_ptr_, cluster_status);
    }

  private:
    friend class ClusterDiscoveryManager;

    CallbackInvoker(ClusterDiscoveryManager& parent, std::string name,
                    CallbackListItemWeakPtr item_weak_ptr)
        : parent_(parent), name_(std::move(name)), item_weak_ptr_(std::move(item_weak_ptr)) {}

    ClusterDiscoveryManager& parent_;
    const std::string name_;
    CallbackListItemWeakPtr item_weak_ptr_;
  };

  ClusterDiscoveryManager(std::string thread_name,
                          ClusterLifecycleCallbackHandler& lifecycle_callbacks_handler);

  /**
   * Invoke the callbacks for the given cluster name. The discovery status is passed to the
   * callbacks. After invoking the callbacks, they are dropped from the manager.
   */
  void processClusterName(absl::string_view name, ClusterDiscoveryStatus cluster_status);

  /**
   * A struct containing a discovery handle, information whether a discovery for a given cluster
   * was already requested in this thread, and an immediate invocation context.
   */
  struct AddedCallbackData {
    ClusterDiscoveryCallbackHandlePtr handle_ptr_;
    bool discovery_in_progress_;
    CallbackInvoker invoker_;
  };

  /**
   * Adds the discovery callback. Returns a handle and a boolean indicating whether this worker
   * thread has already requested the discovery of a cluster with a given name.
   */
  AddedCallbackData addCallback(std::string name, ClusterDiscoveryCallbackPtr callback);

  /**
   * Swaps this manager with another. Used for tests only.
   */
  void swap(ClusterDiscoveryManager& other);

private:
  /**
   * An item in the callbacks list. It contains the iterator to itself inside the callbacks
   * list. Since the list contains shared pointers to items, we know that the iterator is valid as
   * long as the item is alive.
   */
  struct CallbackListItem {
    CallbackListItem(ClusterDiscoveryCallbackPtr callback) : callback_(std::move(callback)) {}

    ClusterDiscoveryCallbackPtr callback_;
    absl::optional<CallbackListIterator> self_iterator_;
  };

  /**
   * An implementation of discovery handle. Destroy it to drop the callback from the discovery
   * manager. It won't stop the discovery process, though.
   */
  class ClusterDiscoveryCallbackHandleImpl : public ClusterDiscoveryCallbackHandle {
  public:
    ClusterDiscoveryCallbackHandleImpl(ClusterDiscoveryManager& parent, std::string name,
                                       CallbackListItemWeakPtr item_weak_ptr)
        : parent_(parent), name_(std::move(name)), item_weak_ptr_(std::move(item_weak_ptr)) {}

    ~ClusterDiscoveryCallbackHandleImpl() override {
      parent_.erase(name_, std::move(item_weak_ptr_));
    }

  private:
    ClusterDiscoveryManager& parent_;
    const std::string name_;
    CallbackListItemWeakPtr item_weak_ptr_;
  };

  /**
   * Invokes a callback stored in the item and removes it from the callbacks list, so it won't be
   * invoked again.
   */
  void invokeCallbackFromItem(absl::string_view name, CallbackListItemWeakPtr item_weak_ptr,
                              ClusterDiscoveryStatus cluster_status);

  /**
   * Extracts the list of callbacks from the pending_clusters_ map. This action invalidates the
   * self iterators in the items, so destroying the handle won't try to erase the element from the
   * list using an invalid iterator.
   */
  CallbackList extractCallbackList(absl::string_view name);
  /**
   * Creates and sets up the callback list item, adds to the list and returns a weak_ptr to the
   * item.
   */
  CallbackListItemWeakPtr addCallbackInternal(CallbackList& list,
                                              ClusterDiscoveryCallbackPtr callback);
  /**
   * Drops the callback item from the discovery manager. It the item wasn't stale, the callback
   * will not be invoked. Called when the discovery handle is destroyed.
   */
  void erase(absl::string_view name, CallbackListItemWeakPtr item_weak_ptr);
  /**
   * Drops the callback item from the discovery manager.
   */
  void eraseItem(absl::string_view name, CallbackListItemSharedPtr item_ptr);
  /**
   * Try to erase a callback from under the given iterator. It returns a boolean value indicating
   * whether the dropped callback was a last one for the given cluster.
   */
  bool eraseFromList(absl::string_view name, CallbackListIterator it);

  std::string thread_name_;
  absl::flat_hash_map<std::string, CallbackList> pending_clusters_;
  std::unique_ptr<ClusterUpdateCallbacks> callbacks_;
  ClusterUpdateCallbacksHandlePtr callbacks_handle_;
};

} // namespace Upstream
} // namespace Envoy
