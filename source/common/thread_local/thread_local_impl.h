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

  // ThreadLocal::Instance
  SlotPtr allocateSlot() override;
  void registerThread(Event::Dispatcher& dispatcher, bool main_thread) override;
  void shutdownGlobalThreading() override;
  void shutdownThread() override;
  Event::Dispatcher& dispatcher() override;

private:
  struct SlotImpl : public Slot {
    SlotImpl(InstanceImpl& parent, uint64_t index) : parent_(parent), index_(index) {}
    ~SlotImpl() { parent_.removeSlot(*this); }

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    void runOnAllThreads(Event::PostCb cb) override { parent_.runOnAllThreads(cb); }
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    const uint64_t index_;
  };

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void removeSlot(SlotImpl& slot);
  void runOnAllThreads(Event::PostCb cb);
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static thread_local ThreadLocalData thread_local_data_;
  std::vector<SlotImpl*> slots_;
  std::list<std::reference_wrapper<Event::Dispatcher>> registered_threads_;
  std::thread::id main_thread_id_;
  Event::Dispatcher* main_thread_dispatcher_{};
  std::atomic<bool> shutdown_{};
};

} // namespace ThreadLocal
} // namespace Envoy
