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
  void shutdownGlobalThreading() override;
  void shutdownThread() override;
  Event::Dispatcher& dispatcher() override;

private:
  // On destruction returns the slot index to the deferred delete queue (detaches it). This allows
  // a slot to be destructed on the main thread while controlling the lifetime of the underlying
  // slot as callbacks drain from workers.
  struct SlotImpl : public Slot {
    SlotImpl(InstanceImpl& parent, uint32_t index);
    ~SlotImpl() override { parent_.recycle(index_); }
    Event::PostCb wrapCallback(Event::PostCb cb);
    static bool currentThreadRegisteredWorker(uint32_t index);
    static ThreadLocalObjectSharedPtr getWorker(uint32_t index);

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) override;
    bool currentThreadRegistered() override;
    void runOnAllThreads(Event::PostCb cb) override;
    void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) override;
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    const uint32_t index_;
    // The following is used to make sure that the slot index is not recycled until there are no
    // more pending callbacks against the slot. This prevents crashes for well behaved
    std::shared_ptr<uint32_t> ref_count_;
    // The following is used to safely verify via weak_ptr that this slot is still alive. This
    // does not prevent all races if a callback does not capture appropriately, but it does fix
    // the common case of a slot destroyed immediately before anything is posted to a worker.
    std::shared_ptr<bool> still_alive_guard_;
  };

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void recycle(uint32_t slot);
  // Cleanup the deferred deletes queue.
  void scheduleCleanup(uint32_t slot);

  void removeSlot(uint32_t slot);
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
  std::list<uint32_t> free_slot_indexes_;

  std::list<std::reference_wrapper<Event::Dispatcher>> registered_threads_;
  std::thread::id main_thread_id_;
  Event::Dispatcher* main_thread_dispatcher_{};
  std::atomic<bool> shutdown_{};

  // Test only.
  friend class ThreadLocalInstanceImplTest;
};

using InstanceImplPtr = std::unique_ptr<InstanceImpl>;

} // namespace ThreadLocal
} // namespace Envoy
