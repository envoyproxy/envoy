#pragma once

#include <functional>
#include <list>

#include "common/common/assert.h"

namespace Envoy {

// RAII cleanup via functor.
class Cleanup {
public:
  Cleanup(std::function<void()> f) : f_(std::move(f)), cancelled_(false) {}
  ~Cleanup() { f_(); }

  void cancel() {
    cancelled_ = true;
    f_ = []() {};
  }

  bool cancelled() { return cancelled_; }

private:
  std::function<void()> f_;
  bool cancelled_;
};

// RAII helper class to add an element to an std::list on construction and erase
// it on destruction, unless the cancel method has been called.
template <class T> class RaiiListElement {
public:
  RaiiListElement(std::list<T>& container, T element) : container_(container), cancelled_(false) {
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
  bool cancelled_;
};

} // namespace Envoy
