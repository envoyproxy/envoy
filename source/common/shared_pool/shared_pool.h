#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <type_traits>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"

#include "common/common/assert.h"
#include "common/common/non_copyable.h"
#include "common/common/thread_synchronizer.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace SharedPool {

/**
 * Used to share objects that have the same content.
 * control the life cycle of shared objects by reference counting
 *
 * Note:  ObjectSharedPool needs to be created in the main thread,
 * all the member methods can only be called in the main thread,
 * it does not have the ownership of object stored internally, the internal storage is weak_ptr,
 * when the internal storage object destructor executes the custom deleter to remove its own
 * weak_ptr from the ObjectSharedPool.
 *
 * There is also a need to ensure that the thread where ObjectSharedPool's destructor is also in the
 * main thread, or that ObjectSharedPool destruct before the program exit
 */
template <typename T, typename HashFunc = std::hash<T>,
          class = typename std::enable_if<std::is_copy_constructible<T>::value>::type>
class ObjectSharedPool : public Singleton::Instance,
                         public std::enable_shared_from_this<ObjectSharedPool<T, HashFunc>>,
                         NonCopyable {
public:
  ObjectSharedPool(Event::Dispatcher& dispatcher)
      : thread_id_(std::this_thread::get_id()), dispatcher_(dispatcher) {}

  void deleteObject(const size_t hash_key) {
    if (std::this_thread::get_id() == thread_id_) {
      // There may be new inserts with the same hash value before deleting the old element,
      // so there is no need to delete it at this time.
      if (object_pool_.find(hash_key) != object_pool_.end() &&
          object_pool_[hash_key].use_count() == 0) {
        object_pool_.erase(hash_key);
      }
    } else {
      // Most of the time, the object's destructor occurs in the main thread, but with some
      // exceptions, it is destructed in the worker thread. In order to keep the object_pool_ thread
      // safe, the deleteObject needs to be delivered to the main thread.
      auto this_shared_ptr = this->shared_from_this();
      // Used for testing to simulate some race condition scenarios
      sync_.syncPoint(DeleteObjectOnMainThread);
      dispatcher_.post([hash_key, this_shared_ptr] { this_shared_ptr->deleteObject(hash_key); });
    }
  }

  std::shared_ptr<T> getObject(const T& obj) {
    ASSERT(std::this_thread::get_id() == thread_id_);
    auto hashed_value = HashFunc{}(obj);
    auto object_it = object_pool_.find(hashed_value);
    if (object_it != object_pool_.end()) {
      auto lock_object = object_it->second.lock();
      if (lock_object) {
        return lock_object;
      }
    }

    auto this_shared_ptr = this->shared_from_this();
    std::shared_ptr<T> obj_shared(new T(obj), [hashed_value, this_shared_ptr](T* ptr) {
      this_shared_ptr->sync().syncPoint(ObjectSharedPool<T>::ObjectDeleterEntry);
      // release ptr as early as possible to avoid exposure of ptr, resulting in undefined behavior.
      delete ptr;
      this_shared_ptr->deleteObject(hashed_value);
    });

    // When inserted, it is possible that the old elements still exist before they can be deleted,
    // and the insertion will fail and therefore need to be overwritten.
    auto [iter, status] = object_pool_.try_emplace(hashed_value, obj_shared);
    if (!status) {
      ASSERT(iter->second.use_count() == 0);
      iter->second = obj_shared;
    }
    return obj_shared;
  }

  std::size_t poolSize() const {
    ASSERT(std::this_thread::get_id() == thread_id_);
    return object_pool_.size();
  }

  /**
   * @return a thread synchronizer object used for reproducing a race-condition in tests.
   */
  Thread::ThreadSynchronizer& sync() { return sync_; }
  static const char DeleteObjectOnMainThread[];
  static const char ObjectDeleterEntry[];

private:
  const std::thread::id thread_id_;
  absl::flat_hash_map<size_t, std::weak_ptr<T>> object_pool_;
  Event::Dispatcher& dispatcher_;
  Thread::ThreadSynchronizer sync_;
};

template <typename T, typename HashFunc, class V>
const char ObjectSharedPool<T, HashFunc, V>::DeleteObjectOnMainThread[] = "delete-object-on-main";

template <typename T, typename HashFunc, class V>
const char ObjectSharedPool<T, HashFunc, V>::ObjectDeleterEntry[] = "deleter-entry";

} // namespace SharedPool
} // namespace Envoy
