#pragma once

#include <functional>
#include <memory>

namespace Envoy {

// RAII cleanup via functor.
class Cleanup {
public:
  Cleanup(std::function<void()> f) : f_(std::move(f)) {}
  ~Cleanup() { f_(); }

private:
  std::function<void()> f_;
};

typedef std::unique_ptr<Cleanup> CleanupPtr;

} // namespace Envoy
