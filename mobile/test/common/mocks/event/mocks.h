#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"

#include "gmock/gmock.h"
#include "library/common/event/provisional_dispatcher.h"

namespace Envoy {
namespace Event {

class MockProvisionalDispatcher : public ProvisionalDispatcher {
public:
  MockProvisionalDispatcher() = default;
  ~MockProvisionalDispatcher() override = default;

  // ProvisionalDispatcher
  void deferredDelete(DeferredDeletablePtr&& to_delete) override {
    deferredDelete_(to_delete.get());
    if (to_delete) {
      to_delete_.push_back(std::move(to_delete));
    }
  }

  envoy_status_t post(std::function<void()> callback) override {
    callbacks_.push_back(callback);
    return post_(callback);
  }

  // Event::ProvisionalDispatcher
  MOCK_METHOD(void, drain, (Event::Dispatcher & event_dispatcher));
  MOCK_METHOD(void, deferredDelete_, (DeferredDeletable * to_delete));
  MOCK_METHOD(envoy_status_t, post_, (std::function<void()> callback));
  MOCK_METHOD(bool, isThreadSafe, (), (const));

  std::list<DeferredDeletablePtr> to_delete_;
  std::list<std::function<void()>> callbacks_;
};

} // namespace Event
} // namespace Envoy
