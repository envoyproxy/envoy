#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "source/extensions/dynamic_modules/abi/abi.h"

#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

// Logs an exception caught at the ABI boundary at error level so a failure inside a module hook
// stays visible.
inline void logAbiException(const char* function_name, const char* what) {
  std::string message(function_name);
  message += ": caught exception at the ABI boundary: ";
  message += what;
  envoy_dynamic_module_callback_log_v2(
      envoy_dynamic_module_type_log_level_Error,
      envoy_dynamic_module_type_module_buffer{message.data(), message.size()},
      envoy_dynamic_module_type_module_buffer{nullptr, 0}, 0);
}

// Runs a module hook body and returns fail_closed if it throws, so a module exception never crosses
// the C ABI boundary and aborts the process. This mirrors the Rust SDK panic barrier. The return
// type is taken from the body, so fail_closed only needs to be convertible to it.
template <class Fail, class Body>
auto failClosed(const char* function_name, Fail&& fail_closed, Body&& body) noexcept
    -> decltype(body()) {
  try {
    return body();
  } catch (const std::exception& e) {
    logAbiException(function_name, e.what());
  } catch (...) {
    logAbiException(function_name, "unknown exception");
  }
  return static_cast<decltype(body())>(std::forward<Fail>(fail_closed));
}

// The void returning counterpart of failClosed.
template <class Body> void failClosedVoid(const char* function_name, Body&& body) noexcept {
  try {
    body();
  } catch (const std::exception& e) {
    logAbiException(function_name, e.what());
  } catch (...) {
    logAbiException(function_name, "unknown exception");
  }
}

/**
 * Implements the CommonHandle process-wide callbacks against the C ABI, as a mixin over the
 * handle interface being implemented. Each concrete config handle derives from
 * CommonHandleImpl<ItsInterface> and so gains all of them without restating any.
 *
 * @tparam Base the config handle interface to implement, which must derive from CommonHandle.
 */
template <class Base> class CommonHandleImpl : public Base {
public:
  bool getRuntimeBool(std::string_view key, bool default_value) override {
    return envoy_dynamic_module_callback_get_runtime_bool(
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, default_value);
  }

  uint64_t getRuntimeInt(std::string_view key, uint64_t default_value) override {
    return envoy_dynamic_module_callback_get_runtime_int(
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, default_value);
  }

  double getRuntimeNumber(std::string_view key, double default_value) override {
    return envoy_dynamic_module_callback_get_runtime_number(
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, default_value);
  }
};

/**
 * Generic Scheduler implementation backed by a host-managed event dispatcher.
 *
 * All host-side scheduler pointer types are typedef void*, so the three ABI functions are
 * passed as non-type template parameters and called with plain void* arguments.
 *
 * @tparam NewFn    ABI function that allocates the native scheduler: void*(void* host_ptr)
 * @tparam CommitFn ABI function that enqueues a task_id on the worker thread:
 *                  void(void* scheduler_ptr, uint64_t task_id)
 * @tparam DeleteFn ABI function that destroys the native scheduler: void(void* scheduler_ptr)
 */
template <void* (*NewFn)(void*), void (*CommitFn)(void*, uint64_t), void (*DeleteFn)(void*)>
class SchedulerImplBase : public Scheduler {
public:
  explicit SchedulerImplBase(void* host_ptr) : scheduler_ptr_(NewFn(host_ptr)) {}

  void schedule(std::function<void()> func) override {
    uint64_t task_id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      task_id = next_task_id_++;
      tasks_[task_id] = std::move(func);
    }
    CommitFn(scheduler_ptr_, task_id);
  }

  void onScheduled(uint64_t task_id) {
    std::function<void()> func;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = tasks_.find(task_id);
      if (it != tasks_.end()) {
        func = std::move(it->second);
        tasks_.erase(it);
      }
    }
    if (func) {
      func();
    }
  }

  ~SchedulerImplBase() override { DeleteFn(scheduler_ptr_); }

private:
  void* scheduler_ptr_{};
  std::mutex mutex_;
  uint64_t next_task_id_{1}; // 0 is reserved.
  std::map<uint64_t, std::function<void()>> tasks_;
};

} // namespace DynamicModules
} // namespace Envoy
