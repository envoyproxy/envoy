#pragma once

#include <functional>
#include <list>

namespace Envoy {

// RAII cleanup via functor.
class Cleanup {
public:
  Cleanup(std::function<void()> f) : f_(std::move(f)) {}
  ~Cleanup() { f_(); }

private:
  std::function<void()> f_;
};

// RAII helper class to add an element to an std::list on construction and erase
// it on destruction.
template <class T> class ListAddAndRemove {
public:
  ListAddAndRemove(std::list<T>& container, T element) : container_(container) {
    it_ = container.emplace(container.begin(), element);
  }
  virtual ~ListAddAndRemove() { container_.erase(it_); }

private:
  std::list<T>& container_;
  typename std::list<T>::iterator it_;
};

} // namespace Envoy
