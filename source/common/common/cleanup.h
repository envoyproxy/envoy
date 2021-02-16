#pragma once

#include <functional>
#include <list>

#include "common/common/assert.h"

namespace Envoy {

// RAII cleanup via functor.
class Cleanup {
public:
  Cleanup(std::function<void()> f) : f_(std::move(f)), canceled_(false) {}
  ~Cleanup() { f_(); }

  void cancel() {
    canceled_ = true;
    f_ = []() {};
  }

  bool canceled() { return canceled_; }

private:
  std::function<void()> f_;
  bool canceled_;
};

// RAII helper class to add an element to an std::list on construction and erase
// it on destruction, unless the cancel method has been called.
template <class T> class RaiiListElement {
public:
  RaiiListElement(std::list<T>& container, T element) : container_(container), canceled_(false) {
    it_ = container.emplace(container.begin(), element);
  }
  virtual ~RaiiListElement() {
    if (!canceled_) {
      erase();
    }
  }

  // Cancel deletion of the element on destruction. This should be called if the iterator has
  // been invalidated, e.g., if the list has been cleared or the element removed some other way.
  void cancel() { canceled_ = true; }

  // Delete the element now, instead of at destruction.
  void erase() {
    ASSERT(!canceled_);
    container_.erase(it_);
    canceled_ = true;
  }

private:
  std::list<T>& container_;
  typename std::list<T>::iterator it_;
  bool canceled_;
};

// RAII helper class to add an element to a std::list held inside an absl::flat_hash_map on
// construction and erase it on destruction, unless the cancel method has been called. If the list
// is empty after removal of the element, the destructor will also remove the list from the map.
template <class Key, class Value> class RaiiMapOfListElement {
public:
  using MapOfList = absl::flat_hash_map<Key, std::list<Value>>;

  template <typename ConvertibleToKey>
  RaiiMapOfListElement(MapOfList& map, const ConvertibleToKey& key, Value value)
      : map_(map), list_(map_.try_emplace(key).first->second), key_(key), canceled_(false) {
    it_ = list_.emplace(list_.begin(), value);
  }

  virtual ~RaiiMapOfListElement() {
    if (!canceled_) {
      erase();
    }
  }

  void cancel() { canceled_ = true; }

private:
  void erase() {
    ASSERT(!canceled_);
    list_.erase(it_);
    if (list_.empty()) {
      map_.erase(key_);
    }
    canceled_ = true;
  }

  MapOfList& map_;
  std::list<Value>& list_;
  // Because of absl::flat_hash_map iterator instability we have to keep a copy of the key
  const Key key_;
  typename MapOfList::mapped_type::iterator it_;
  bool canceled_;
};
} // namespace Envoy
