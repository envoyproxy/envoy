#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/common/non_copyable.h"

#include "absl/container/flat_hash_set.h"

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

  // A Wrapper of SlotImpl which on destruction returns the SlotImpl to the deferred delete queue
  // (detaches it). fixfix
  struct SlotImpl : public Slot {
    SlotImpl(InstanceImpl& parent, uint64_t index);
    ~SlotImpl() override { parent_.recycle(index_); }
    Event::PostCb wrapCallback(Event::PostCb cb);

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) override;
    bool currentThreadRegistered() override;
    void runOnAllThreads(Event::PostCb cb) override;
    void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) override;
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    const uint64_t index_;
    std::shared_ptr<uint32_t> ref_count_;
    // The following is used to safely verify via weak_ptr that this slot is still alive.
    std::shared_ptr<bool> still_alive_guard_;
  };

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void recycle(uint64_t slot);
  // Cleanup the deferred deletes queue.
  void scheduleCleanup(uint64_t slot);

  void removeSlot(uint64_t slot);
  void runOnAllThreads(Event::PostCb cb);
  void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback);
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static thread_local ThreadLocalData thread_local_data_;

  // A indexed container for Slots that has to be deferred to delete due to out-going callbacks
  // pointing to the Slot. To let the ref_count_ deleter find the SlotImpl by address, the container
  // is defined as a map of SlotImpl address to the unique_ptr<SlotImpl>.
  absl::flat_hash_set<uint32_t> deferred_deletes_;

  std::vector<Slot*> slots_;
  // A list of index of freed slots.
  std::list<uint64_t> free_slot_indexes_;

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
