#pragma once

#include <functional>

namespace Envoy {

// RAII cleanup via functor.
class Cleanup {
public:
  Cleanup(std::function<void()> f) : f_(std::move(f)) {}
  ~Cleanup() { f_(); }

private:
  std::function<void()> f_;
};

} // namespace Envoy
