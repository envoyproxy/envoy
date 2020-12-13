#pragma once

#include <stdexcept>
#include <string>
#include <thread>

namespace Envoy {
/**
 * Base class for all envoy exceptions.
 */
#define envoy_try \
assert(isMainThread()); try 

class EnvoyException : public std::runtime_error {
public:
  EnvoyException(const std::string& message) : std::runtime_error(message) {}
};


static bool isMainThread() {
    static std::thread::id main_thread_id = std::this_thread::get_id();
    return main_thread_id == std::this_thread::get_id(); 
}



} // namespace Envoy
