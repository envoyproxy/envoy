#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <type_traits>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"

#include "source/common/common/assert.h"
#include "source/common/common/non_copyable.h"
#include "source/common/common/thread_synchronizer.h"

#include "absl/container/flat_hash_set.h"

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
template <typename T, typename HashFunc = std::hash<T>, typename EqualFunc = std::equal_to<T>,
          class = typename std::enable_if<std::is_copy_constructible<T>::value>::type>
class ObjectSharedPool
    : public Singleton::Instance,
      public std::enable_shared_from_this<ObjectSharedPool<T, HashFunc, EqualFunc>>,
      NonCopyable {
public:
  ObjectSharedPool(Event::Dispatcher& dispatcher)
      : thread_id_(std::this_thread::get_id()), dispatcher_(dispatcher) {}

  std::shared_ptr<T> getObject(const T& obj) {
    ASSERT(std::this_thread::get_id() == thread_id_);

    // Return from the object pool if we find the object there.
    if (auto iter = object_pool_.find(&obj); iter != object_pool_.end()) {
      if (auto lock_object = iter->lock(); static_cast<bool>(lock_object) == true) {
        return lock_object;
      } else {
        // Remove the weak_ptr since all associated shared_ptrs have been
        // destroyed.
        object_pool_.erase(iter);
      }
    }

    // Create a shared_ptr and add the object to the object_pool.
    auto this_shared_ptr = this->shared_from_this();
    std::shared_ptr<T> obj_shared(new T(obj), [this_shared_ptr](T* ptr) {
      this_shared_ptr->sync().syncPoint(ObjectSharedPool<T>::ObjectDeleterEntry);
      this_shared_ptr->deleteObject(ptr);
    });
    object_pool_.emplace(obj_shared);
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

  friend class SharedPoolTest;

private:
  void deleteObject(T* ptr) {
    if (std::this_thread::get_id() == thread_id_) {
      deleteObjectOnMainThread(ptr);
    } else {
      // Most of the time, the object's destructor occurs in the main thread, but with some
      // exceptions, it is destructed in the worker thread. In order to keep the object_pool_ thread
      // safe, the deleteObject needs to be delivered to the main thread.
      auto this_shared_ptr = this->shared_from_this();
      // Used for testing to simulate some race condition scenarios
      sync_.syncPoint(DeleteObjectOnMainThread);
      dispatcher_.post([ptr, this_shared_ptr] { this_shared_ptr->deleteObjectOnMainThread(ptr); });
    }
  }

  void deleteObjectOnMainThread(T* ptr) {
    ASSERT(std::this_thread::get_id() == thread_id_);
    if (auto iter = object_pool_.find(ptr); iter != object_pool_.end()) {
      // It is possible that the entry in object_pool_ corresponds to a
      // different weak_ptr, due to a race condition in a shared_ptr being
      // destroyed on another thread, and getObject() being called on the main
      // thread.
      if (iter->use_count() == 0) {
        object_pool_.erase(iter);
      }
    }
    // Wait till here to delete the pointer because we don't want the OS to
    // reallocate the memory location before this method completes to prevent
    // "hash collisions".
    delete ptr;
  }

  class Element {
  public:
    Element(const std::shared_ptr<T>& ptr) : ptr_{ptr.get()}, weak_ptr_{ptr} {}

    Element() = delete;
    Element(const Element&) = delete;

    Element(Element&&) noexcept = default;

    std::shared_ptr<T> lock() const { return weak_ptr_.lock(); }
    long use_count() const { return weak_ptr_.use_count(); }

    friend struct Hash;
    friend struct Compare;

    struct Hash {
      using is_transparent = void; // NOLINT(readability-identifier-naming)
      constexpr size_t operator()(const T* ptr) const { return HashFunc{}(*ptr); }
      constexpr size_t operator()(const Element& element) const {
        return HashFunc{}(*element.ptr_);
      }
    };
    struct Compare {
      using is_transparent = void; // NOLINT(readability-identifier-naming)
      bool operator()(const Element& a, const Element& b) const {
        ASSERT(a.ptr_ != nullptr && b.ptr_ != nullptr);
        return a.ptr_ == b.ptr_ ||
               (a.ptr_ != nullptr && b.ptr_ != nullptr && EqualFunc{}(*a.ptr_, *b.ptr_));
      }
      bool operator()(const Element& a, const T* ptr) const {
        ASSERT(a.ptr_ != nullptr && ptr != nullptr);
        return a.ptr_ == ptr || (a.ptr_ != nullptr && ptr != nullptr && EqualFunc{}(*a.ptr_, *ptr));
      }
    };

  private:
    const T* const ptr_ = nullptr; ///< This is only used to speed up
                                   ///< comparisons and should never be
                                   ///< made available outside this class.
    std::weak_ptr<T> weak_ptr_;
  };

  const std::thread::id thread_id_;
  absl::flat_hash_set<Element, typename Element::Hash, typename Element::Compare> object_pool_;
  // Use a multimap to allow for multiple objects with the same hash key.
  // std::unordered_multimap<size_t, std::weak_ptr<T>> object_pool_;
  Event::Dispatcher& dispatcher_;
  Thread::ThreadSynchronizer sync_;
};

template <typename T, typename HashFunc, typename EqualFunc, class V>
const char ObjectSharedPool<T, HashFunc, EqualFunc, V>::DeleteObjectOnMainThread[] =
    "delete-object-on-main";

template <typename T, typename HashFunc, typename EqualFunc, class V>
const char ObjectSharedPool<T, HashFunc, EqualFunc, V>::ObjectDeleterEntry[] = "deleter-entry";

} // namespace SharedPool
} // namespace Envoy
