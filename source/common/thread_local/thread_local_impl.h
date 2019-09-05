#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <vector>

#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"
#include "common/common/non_copyable.h"

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

    // non moveable.
    SlotImpl(SlotImpl&&) = delete;
    SlotImpl& operator=(SlotImpl&&) = delete;

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

  // A helper class for holding a SlotImpl and its bookkeeping shared_ptr which counts the number of
  // update callbacks on-the-fly.
  struct SlotHolder {
    SlotHolder(std::unique_ptr<SlotImpl>&& slot) : slot_(std::move(slot)) {}
    bool isRecycleable() { return ref_count_.use_count() == 1; }

    std::unique_ptr<SlotImpl> slot_;
    std::shared_ptr<int> ref_count_{new int(0)};
  };

  // A Wrapper of SlotImpl which on destruction returns the SlotImpl to the deferred delete queue
  // (detaches it).
  struct Bookkeeper : public Slot {
    Bookkeeper(InstanceImpl& parent, std::unique_ptr<SlotImpl>&& slot);
    ~Bookkeeper() override { parent_.recycle(std::move(holder_)); }
    SlotImpl& slot() { return *(holder_->slot_); }

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) override;
    bool currentThreadRegistered() override;
    void runOnAllThreads(Event::PostCb cb) override;
    void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) override;
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    std::unique_ptr<SlotHolder> holder_;
  };

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void recycle(std::unique_ptr<SlotHolder>&& holder);
  // Cleanup the deferred deletes queue.
  void scheduleCleanup();
  void removeSlot(SlotImpl& slot);
  void runOnAllThreads(Event::PostCb cb);
  void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback);
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static thread_local ThreadLocalData thread_local_data_;

  // A queue for Slots that has to be deferred to delete due to out-going callbacks
  // pointing to the Slot.
  std::list<std::unique_ptr<SlotHolder>> deferred_deletes_;

  std::vector<SlotImpl*> slots_;
  // A list of index of freed slots.
  std::list<uint32_t> free_slot_indexes_;

  std::list<std::reference_wrapper<Event::Dispatcher>> registered_threads_;
  std::thread::id main_thread_id_;
  Event::Dispatcher* main_thread_dispatcher_{};
  std::atomic<bool> shutdown_{};
};

} // namespace ThreadLocal
} // namespace Envoy
