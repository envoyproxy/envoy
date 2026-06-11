#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>

#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

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
