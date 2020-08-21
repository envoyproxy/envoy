#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/common/non_copyable.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace ThreadLocal {

/**
 * Implementation of ThreadLocal that relies on static thread_local objects.
 */
class InstanceImpl : Logger::Loggable<Logger::Id::main>, public NonCopyable, public Instance {
public:
  InstanceImpl() : main_thread_id_(std::this_thread::get_id()) {}
  ~InstanceImpl() override;

  // ThreadLocal::Instance
  SlotPtr allocateSlot() override;
  void registerThread(Event::Dispatcher& dispatcher, bool main_thread) override;
  void startGlobalThreading() override;
  void shutdownGlobalThreading() override;
  void shutdownThread() override;
  Event::Dispatcher& dispatcher() override;

private:
  enum class State {
    Initializing, // TLS is initializing and no worker threads are running yet.
    Running,      // TLS is running with worker threads.
    Shutdown      // Worker threads are about to shut down.
  };

  struct SlotImpl : public Slot {
    SlotImpl(InstanceImpl& parent, uint64_t index);
    ~SlotImpl() override;
    Event::PostCb wrapCallback(Event::PostCb cb);

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    bool currentThreadRegistered() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) override;
    void runOnAllThreads(Event::PostCb cb) override;
    void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) override;
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    const uint64_t index_;
    // The following is used to safely verify via weak_ptr that this slot is still alive.
    std::shared_ptr<bool> still_alive_guard_;
    // The following is used to reference count active callbacks. When TLS is in the running
    // state destruction will block until all callbacks have been flushed.
    std::shared_ptr<bool> active_callback_guard_;
    // The following are used to synchronize blocking for flushing while in the running state.
    bool ready_to_destroy_ ABSL_GUARDED_BY(shutdown_mutex_);
    absl::Mutex shutdown_mutex_;
  };

  using SlotImplPtr = std::unique_ptr<SlotImpl>;

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void removeSlot(SlotImpl& slot);
  void runOnAllThreads(Event::PostCb cb);
  void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback);
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static thread_local ThreadLocalData thread_local_data_;

  std::vector<SlotImpl*> slots_;
  // A list of index of freed slots.
  std::list<uint32_t> free_slot_indexes_;

  std::list<std::reference_wrapper<Event::Dispatcher>> registered_threads_;
  std::thread::id main_thread_id_;
  Event::Dispatcher* main_thread_dispatcher_{};
  std::atomic<State> state_{State::Initializing};

  // Test only.
  friend class ThreadLocalInstanceImplTest;
};

using InstanceImplPtr = std::unique_ptr<InstanceImpl>;

} // namespace ThreadLocal
} // namespace Envoy
