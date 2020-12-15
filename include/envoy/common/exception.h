#pragma once

#include <stdexcept>
#include <string>
#include <thread>

namespace Envoy {
/**
 * Base class for all envoy exceptions.
 */
#define envoy_try                                                                                  \
  ASSERT(MainThreadSingleton::get().isMainThread());                                               \
  try

#define MainThreadSingleton InjectableSingleton<MainThread>

class EnvoyException : public std::runtime_error {
public:
  EnvoyException(const std::string& message) : std::runtime_error(message) {}
};

struct MainThread {
  MainThread() : main_thread_id_{std::this_thread::get_id()} {}
  bool isMainThread() { return main_thread_id_ == std::this_thread::get_id(); }

private:
  const std::thread::id main_thread_id_;
};

} // namespace Envoy
