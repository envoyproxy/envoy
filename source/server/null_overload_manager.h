#pragma once

#include "envoy/server/overload/overload_manager.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/event/scaled_range_timer_manager_impl.h"

namespace Envoy {
namespace Server {

/** Implementation of OverloadManager that is never overloaded. Using this instead of the real
 *  OverloadManager keeps the admin interface accessible even when the proxy is overloaded.
 */
class NullOverloadManager : public OverloadManager {
public:
  struct OverloadState : public ThreadLocalOverloadState {
    OverloadState(Event::Dispatcher& dispatcher, bool permissive)
        : dispatcher_(dispatcher), permissive_(permissive) {}
    const OverloadActionState& getState(const std::string&) override { return inactive_; }
    bool tryAllocateResource(OverloadProactiveResourceName, int64_t) override {
      return permissive_;
    }
    bool tryDeallocateResource(OverloadProactiveResourceName, int64_t) override {
      return permissive_;
    }
    bool isResourceMonitorEnabled(OverloadProactiveResourceName) override { return false; }
    ProactiveResourceMonitorOptRef
    getProactiveResourceMonitorForTest(OverloadProactiveResourceName) override {
      return makeOptRefFromPtr<ProactiveResourceMonitor>(nullptr);
    }
    Event::Dispatcher& dispatcher_;
    const bool permissive_;
    const OverloadActionState inactive_ = OverloadActionState::inactive();
  };

  NullOverloadManager(ThreadLocal::SlotAllocator& slot_allocator, bool permissive)
      : tls_(slot_allocator.allocateSlot()), permissive_(permissive) {}

  void start() override {
    tls_->set([this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<OverloadState>(dispatcher, permissive_);
    });
  }

  ThreadLocalOverloadState& getThreadLocalOverloadState() override {
    return tls_->getTyped<OverloadState>();
  }

  LoadShedPoint* getLoadShedPoint(absl::string_view) override { return nullptr; }

  Event::ScaledRangeTimerManagerFactory scaledTimerFactory() override {
    if (!permissive_) {
      return nullptr;
    }
    return [](Event::Dispatcher& dispatcher) {
      return std::make_unique<Event::ScaledRangeTimerManagerImpl>(dispatcher, nullptr);
    };
  }

  bool registerForAction(const std::string&, Event::Dispatcher&, OverloadActionCb) override {
    return true;
  }
  void stop() override {}

  ThreadLocal::SlotPtr tls_;
  // The admin code runs in non-permissive mode, rejecting connections and
  // ensuring timer code is not called. Envoy mobile uses permissive mode and
  // does the opposite.
  const bool permissive_;
};

} // namespace Server
} // namespace Envoy
