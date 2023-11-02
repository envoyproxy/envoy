#pragma once

#include "envoy/server/overload/overload_manager.h"

namespace Envoy {
namespace Server {

/** * Implementation of OverloadManager that is never overloaded. Using this instead of the real
 * * OverloadManager keeps the admin interface accessible even when the proxy is overloaded. */
class NullOverloadManager : public OverloadManager {
public:
  struct OverloadState : public ThreadLocalOverloadState {
    OverloadState(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
    const OverloadActionState& getState(const std::string&) override { return inactive_; }
    bool tryAllocateResource(OverloadProactiveResourceName, int64_t) override { return false; }
    bool tryDeallocateResource(OverloadProactiveResourceName, int64_t) override { return false; }
    bool isResourceMonitorEnabled(OverloadProactiveResourceName) override { return false; }
    ProactiveResourceMonitorOptRef
    getProactiveResourceMonitorForTest(OverloadProactiveResourceName) override {
      return makeOptRefFromPtr<ProactiveResourceMonitor>(nullptr);
    }
    Event::Dispatcher& dispatcher_;
    const OverloadActionState inactive_ = OverloadActionState::inactive();
  };

  NullOverloadManager(ThreadLocal::SlotAllocator& slot_allocator)
      : tls_(slot_allocator.allocateSlot()) {}

  void start() override {
    tls_->set([](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<OverloadState>(dispatcher);
    });
  }

  ThreadLocalOverloadState& getThreadLocalOverloadState() override {
    return tls_->getTyped<OverloadState>();
  }
  LoadShedPoint* getLoadShedPoint(absl::string_view) override { return nullptr; }

  Event::ScaledRangeTimerManagerFactory scaledTimerFactory() override { return nullptr; }

  bool registerForAction(const std::string&, Event::Dispatcher&, OverloadActionCb) override {
    // This method shouldn't be called by the admin listener
    IS_ENVOY_BUG("Unexpected function call");
    return false;
  }
  void stop() override {}

  ThreadLocal::SlotPtr tls_;
};

} // namespace Server
} // namespace Envoy
