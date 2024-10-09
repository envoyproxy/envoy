#pragma once

#include <functional>
#include <list>

#include "source/common/common/assert.h"

namespace Envoy {

// RAII cleanup via functor.
class Cleanup {
public:
  Cleanup(std::function<void()> f) : f_(std::move(f)) {}
  ~Cleanup() { f_(); }

  // Copying leads to cleanup multiple times, so only allow move.
  Cleanup(const Cleanup&) = delete;
  Cleanup(Cleanup&&) = default;

  void cancel() {
    cancelled_ = true;
    f_ = []() {};
  }

  bool cancelled() { return cancelled_; }

  static Cleanup Noop() {
    return Cleanup([] {});
  }

private:
  std::function<void()> f_;
  bool cancelled_{false};
};

// RAII helper class to add an element to an std::list on construction and erase
// it on destruction, unless the cancel method has been called.
template <class T> class RaiiListElement {
public:
  RaiiListElement(std::list<T>& container, T element) : container_(container) {
    it_ = container.emplace(container.begin(), element);
  }
  virtual ~RaiiListElement() {
    if (!cancelled_) {
      erase();
    }
  }

  // Cancel deletion of the element on destruction. This should be called if the iterator has
  // been invalidated, e.g., if the list has been cleared or the element removed some other way.
  void cancel() { cancelled_ = true; }

  // Delete the element now, instead of at destruction.
  void erase() {
    ASSERT(!cancelled_);
    container_.erase(it_);
    cancelled_ = true;
  }

private:
  std::list<T>& container_;
  typename std::list<T>::iterator it_;
  bool cancelled_{false};
};

// RAII helper class to add an element to a std::list held inside an absl::flat_hash_map on
// construction and erase it on destruction, unless the cancel method has been called. If the list
// is empty after removal of the element, the destructor will also remove the list from the map.
template <class Key, class Value> class RaiiMapOfListElement {
public:
  using MapOfList = absl::flat_hash_map<Key, std::list<Value>>;

  template <typename ConvertibleToKey>
  RaiiMapOfListElement(MapOfList& map, const ConvertibleToKey& key, Value value)
      : map_(map), key_(key) {
    // The list reference itself cannot be saved because it is not stable in the event of a
    // absl::flat_hash_map rehash.
    std::list<Value>& list = map_.try_emplace(key).first->second;
    it_ = list.emplace(list.begin(), value);
  }

  virtual ~RaiiMapOfListElement() {
    if (!cancelled_) {
      erase();
    }
  }

  void cancel() { cancelled_ = true; }

private:
  void erase() {
    ASSERT(!cancelled_);
    auto list_it = map_.find(key_);
    ASSERT(list_it != map_.end());

    list_it->second.erase(it_);
    if (list_it->second.empty()) {
      map_.erase(key_);
    }
    cancelled_ = true;
  }

  MapOfList& map_;
  // Because of absl::flat_hash_map iterator instability we have to keep a copy of the key.
  const Key key_;
  typename MapOfList::mapped_type::iterator it_;
  bool cancelled_{false};
};
} // namespace Envoy
