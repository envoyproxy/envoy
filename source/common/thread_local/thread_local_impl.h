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
  void shutdownGlobalThreading() override;
  void shutdownThread() override;
  Event::Dispatcher& dispatcher() override;

private:
  struct SlotImpl : public Slot {
    SlotImpl(InstanceImpl& parent, uint64_t index) : parent_(parent), index_(index) {}
    ~SlotImpl() override { parent_.removeSlot(*this); }

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    bool currentThreadRegistered() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) override;
    void runOnAllThreads(Event::PostCb cb) override { parent_.runOnAllThreads(cb); }
    void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) override {
      parent_.runOnAllThreads(cb, main_callback);
    }
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    const uint64_t index_;
  };

  using SlotImplPtr = std::unique_ptr<SlotImpl>;

  // A Wrapper of SlotImpl which on destruction returns the SlotImpl to the deferred delete queue
  // (detaches it).
  struct Bookkeeper : public Slot {
    Bookkeeper(InstanceImpl& parent, SlotImplPtr&& slot);
    ~Bookkeeper() override { parent_.recycle(std::move(slot_)); }

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) override;
    bool currentThreadRegistered() override;
    void runOnAllThreads(Event::PostCb cb) override;
    void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) override;
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    SlotImplPtr slot_;
    std::shared_ptr<uint32_t> ref_count_;
  };

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void recycle(SlotImplPtr&& slot);
  // Cleanup the deferred deletes queue.
  void scheduleCleanup(SlotImpl* slot);

  void removeSlot(SlotImpl& slot);
  void runOnAllThreads(Event::PostCb cb);
  void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback);
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static thread_local ThreadLocalData thread_local_data_;

  // A indexed container for Slots that has to be deferred to delete due to out-going callbacks
  // pointing to the Slot. To let the ref_count_ deleter find the SlotImpl by address, the container
  // is defined as a map of SlotImpl address to the unique_ptr<SlotImpl>.
  absl::flat_hash_map<SlotImpl*, SlotImplPtr> deferred_deletes_;

  std::vector<SlotImpl*> slots_;
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
