#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <type_traits>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"

#include "common/common/assert.h"
#include "common/common/non_copyable.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace SharedPool {

/**
 * Used to share objects that have the same content.
 * control the life cycle of shared objects by reference counting
 * Note: this class has only deleteObject for thread safety, and the other member methods are
 * non-thread safe
 */
template <typename T, typename HashFunc = std::hash<T>,
          class = typename std::enable_if<std::is_copy_constructible<T>::value>::type>
class ObjectSharedPool : public Singleton::Instance,
                         public std::enable_shared_from_this<ObjectSharedPool<T, HashFunc>>,
                         NonCopyable {
public:
  ObjectSharedPool(Event::Dispatcher& dispatcher)
      : thread_id_(std::this_thread::get_id()), dispatcher_(dispatcher) {}
  ~ObjectSharedPool() = default;

  void deleteObject(const size_t hash_key) {
    if (std::this_thread::get_id() == thread_id_) {
      object_pool_.erase(hash_key);
    } else {
      auto this_shared_ptr = this->shared_from_this();
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
      this_shared_ptr->deleteObject(hashed_value);
      delete ptr;
    });
    object_pool_.try_emplace(hashed_value, obj_shared);
    return obj_shared;
  }

  std::size_t poolSize() const {
    ASSERT(std::this_thread::get_id() == thread_id_);
    return object_pool_.size();
  }

  void setThreadIdForTest(const std::thread::id& thread_id) { thread_id_ = thread_id; }

private:
  std::thread::id thread_id_;
  absl::flat_hash_map<size_t, std::weak_ptr<T>> object_pool_;
  Event::Dispatcher& dispatcher_;
};

} // namespace SharedPool
} // namespace Envoy
