#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/common/non_copyable.h"

namespace Envoy {
namespace ThreadLocal {

/**
 * Implementation of ThreadLocal that relies on static thread_local objects.
 */
class InstanceImpl : Logger::Loggable<Logger::Id::main>, public NonCopyable, public Instance {
public:
  InstanceImpl();
  ~InstanceImpl() override;

  // ThreadLocal::Instance
  SlotSharedPtr allocateSlot() override;
  void registerThread(Event::Dispatcher& dispatcher, bool main_thread) override;
  void shutdownGlobalThreading() override;
  void shutdownThread() override;
  Event::Dispatcher& dispatcher() override;
  bool isShutdown() const override { return shutdown_; }

private:
  // On destruction returns the slot index to the deferred delete queue (detaches it). This allows
  // a slot to be destructed on the main thread while controlling the lifetime of the underlying
  // slot as callbacks drain from workers.
  struct SlotImpl : public Slot, public std::enable_shared_from_this<SlotImpl> {
    SlotImpl(InstanceImpl& parent, uint32_t index);
    ~SlotImpl() override;
    std::function<void()> wrapCallback(const std::function<void()>& cb);
    std::function<void()> dataCallback(const UpdateCb& cb);
    static bool currentThreadRegisteredWorker(uint32_t index);
    static ThreadLocalObjectSharedPtr getWorker(uint32_t index);

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override;
    void runOnAllThreads(const UpdateCb& cb) override;
    void runOnAllThreads(const UpdateCb& cb, const std::function<void()>& complete_cb) override;
    bool currentThreadRegistered() override;
    void set(InitializeCb cb) override;
    bool isShutdown() const override { return isShutdownImpl(); }
    // We need to call isShutdown inside the destructor, so it must be non-virtual.
    bool isShutdownImpl() const { return parent_.shutdown_; }

    InstanceImpl& parent_;
    const uint32_t index_;
    InitializeCb initialize_cb_;
  };

  struct ThreadLocalData {
    Event::Dispatcher* dispatcher_{};
    std::vector<ThreadLocalObjectSharedPtr> data_;
  };

  void removeSlot(uint32_t slot);
  void runOnAllThreads(std::function<void()> cb);
  void runOnAllThreads(std::function<void()> cb, std::function<void()> main_callback);
  static void setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object);

  static thread_local ThreadLocalData thread_local_data_;

  Thread::MainThread main_thread_;
  std::vector<std::weak_ptr<SlotImpl>> slots_;
  // A collection of indices of freed slots.
  std::vector<uint32_t> free_slot_indexes_;
  std::list<std::reference_wrapper<Event::Dispatcher>> registered_threads_;
  Event::Dispatcher* main_thread_dispatcher_{};
  std::atomic<bool> shutdown_{false};

  // Test only.
  friend class ThreadLocalInstanceImplTest;
};

using InstanceImplPtr = std::unique_ptr<InstanceImpl>;

} // namespace ThreadLocal
} // namespace Envoy
