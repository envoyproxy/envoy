#pragma once

#include <cstdint>

#include "envoy/thread_local/thread_local.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace ThreadLocal {

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  MOCK_METHOD(void, runOnAllThreads, (Event::PostCb cb));
  MOCK_METHOD(void, runOnAllThreads, (Event::PostCb cb, Event::PostCb main_callback));

  // Server::ThreadLocal
  MOCK_METHOD(SlotPtr, allocateSlot, ());
  MOCK_METHOD(void, registerThread, (Event::Dispatcher & dispatcher, bool main_thread));
  MOCK_METHOD(void, shutdownGlobalThreading, ());
  MOCK_METHOD(void, shutdownThread, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());

  SlotPtr allocateSlot_() { return SlotPtr{new SlotImpl(*this, current_slot_++)}; }
  void runOnAllThreads1_(Event::PostCb cb) { cb(); }
  void runOnAllThreads2_(Event::PostCb cb, Event::PostCb main_callback) {
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
      parent_.deferred_data_.resize(index_ + 1);
    }

    ~SlotImpl() override {
      // Do not actually clear slot data during shutdown. This mimics the production code.
      // The defer_delete mimics the recycle() code with Bookkeeper.
      if (!parent_.shutdown_ && !parent_.defer_delete) {
        EXPECT_LT(index_, parent_.data_.size());
        parent_.data_[index_].reset();
      }
    }

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override { return parent_.data_[index_]; }
    bool currentThreadRegistered() override { return parent_.registered_; }
    void runOnAllThreads(const UpdateCb& cb) override {
      parent_.runOnAllThreads([cb, this]() { parent_.data_[index_] = cb(parent_.data_[index_]); });
    }
    void runOnAllThreads(const UpdateCb& cb, Event::PostCb main_callback) override {
      parent_.runOnAllThreads([cb, this]() { parent_.data_[index_] = cb(parent_.data_[index_]); },
                              main_callback);
    }

    void set(InitializeCb cb) override {
      if (parent_.defer_data) {
        parent_.deferred_data_[index_] = cb;
      } else {
        parent_.data_[index_] = cb(parent_.dispatcher_);
      }
    }

    MockInstance& parent_;
    const uint32_t index_;
  };

  void call() {
    for (unsigned i = 0; i < deferred_data_.size(); i++) {
      data_[i] = deferred_data_[i](dispatcher_);
    }
    deferred_data_.clear();
  }

  uint32_t current_slot_{};
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<ThreadLocalObjectSharedPtr> data_;
  std::vector<Slot::InitializeCb> deferred_data_;
  bool defer_data{};
  bool shutdown_{};
  bool registered_{true};
  bool defer_delete{};
};

} // namespace ThreadLocal
} // namespace Envoy
