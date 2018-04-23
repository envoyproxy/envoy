#pragma once

#include <cstdint>
#include <unordered_map>

#include "envoy/thread_local/thread_local.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace ThreadLocal {

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  MOCK_METHOD1(runOnAllThreads, void(Event::PostCb cb));

  // Server::ThreadLocal
  MOCK_METHOD0(allocateSlot, SlotPtr());
  MOCK_METHOD2(registerThread, void(Event::Dispatcher& dispatcher, bool main_thread));
  MOCK_METHOD0(shutdownGlobalThreading, void());
  MOCK_METHOD0(shutdownThread, void());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());

  SlotPtr allocateSlot_() { return SlotPtr{new SlotImpl(*this, current_slot_++)}; }
  void runOnAllThreads_(Event::PostCb cb) { cb(); }
  void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) {
    cb();
    main_callback();
  }

  void shutdownThread_() {
    shutdown_ = true;
    // Reverse order which is same as the production code.
    for (auto it = data_.rbegin(); it != data_.rend(); ++it) {
      it->reset();
    }
    data_.clear();
  }

  struct SlotImpl : public Slot {
    SlotImpl(MockInstance& parent, uint32_t index) : parent_(parent), index_(index) {
      parent_.data_.resize(index_ + 1);
    }

    ~SlotImpl() {
      // Do not actually clear slot data during shutdown. This mimics the production code.
      if (!parent_.shutdown_) {
        EXPECT_LT(index_, parent_.data_.size());
        parent_.data_[index_].reset();
      }
    }

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override { return parent_.data_[index_]; }
    void runOnAllThreads(Event::PostCb cb) override { parent_.runOnAllThreads(cb); }
    void runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) override {
      parent_.runOnAllThreads(cb, main_callback);
    }
    void set(InitializeCb cb) override { parent_.data_[index_] = cb(parent_.dispatcher_); }

    MockInstance& parent_;
    const uint32_t index_;
  };

  uint32_t current_slot_{};
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<ThreadLocalObjectSharedPtr> data_;
  bool shutdown_{};
};

} // namespace ThreadLocal
} // namespace Envoy
