#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
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
  // On destruction returns the slot index to the deferred delete queue (detaches it). This allows
  // a slot to be destructed on the main thread while controlling the lifetime of the underlying
  // slot as callbacks drain from workers.
  struct SlotImpl : public Slot {
    SlotImpl(InstanceImpl& parent, uint32_t index);
    ~SlotImpl() override { parent_.removeSlot(index_); }
    Event::PostCb wrapCallback(Event::PostCb&& cb);
    static bool currentThreadRegisteredWorker(uint32_t index);
    static ThreadLocalObjectSharedPtr getWorker(uint32_t index);

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) override;
    bool currentThreadRegistered() override;
    void set(InitializeCb cb) override;

    InstanceImpl& parent_;
    const uint32_t index_;
    // The following is used to safely verify via weak_ptr that this slot is still alive. This
    // does not prevent all races if a callback does not capture appropriately, but it does fix
    // the common case of a slot destroyed immediately before anything is posted to a worker.
    // NOTE: The general safety model of a slot is that it is destroyed immediately on the main
    //       thread. This means that *all* captures must not reference the slot object directly.
    //       this is why index_ is captured manually in callbacks that require it.
    // NOTE: When the slot is destroyed, the index is immediately recycled. This is safe because
    //       any new posts for a recycled index must come after any previous callbacks for the
    //       previous owner of the index.
    // TODO(mattklein123): Add clang-tidy analysis rule to check that "this" is not captured by
    // a TLS function call. This check will not prevent all bad captures, but it will at least
    // make the programmer more aware of potential issues.
    std::shared_ptr<bool> still_alive_guard_;
  };

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void removeSlot(uint32_t slot);
  void runOnAllThreads(Event::PostCb cb);
  void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback);
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static thread_local ThreadLocalData thread_local_data_;

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
