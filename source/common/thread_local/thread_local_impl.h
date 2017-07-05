#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <vector>

#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"

namespace Envoy {
namespace ThreadLocal {

/**
 * Implementation of ThreadLocal that relies on static thread_local objects.
 */
class InstanceImpl : Logger::Loggable<Logger::Id::main>, public Instance {
public:
  InstanceImpl() : main_thread_id_(std::this_thread::get_id()) {}
  ~InstanceImpl();

  // Server::ThreadLocal
  uint32_t allocateSlot() override { return next_slot_id_++; }
  ThreadLocalObjectSharedPtr get(uint32_t index) override;
  void registerThread(Event::Dispatcher& dispatcher, bool main_thread) override;
  void runOnAllThreads(Event::PostCb cb) override;
  void set(uint32_t index, InitializeCb cb) override;
  void shutdownThread() override;

private:
  struct ThreadLocalData {
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void reset();
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static std::atomic<uint32_t> next_slot_id_;
  static thread_local ThreadLocalData thread_local_data_;
  static std::list<std::reference_wrapper<Event::Dispatcher>> registered_threads_;
  std::thread::id main_thread_id_;
  Event::Dispatcher* main_thread_dispatcher_{};
};

} // namespace ThreadLocal
} // namespace Envoy
