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

  MOCK_METHOD(void, runOnAllThreads, (std::function<void()> cb));
  MOCK_METHOD(void, runOnAllThreads,
              (std::function<void()> cb, std::function<void()> main_callback));

  // Server::ThreadLocal
  MOCK_METHOD(SlotPtr, allocateSlot, ());
  MOCK_METHOD(void, registerThread, (Event::Dispatcher & dispatcher, bool main_thread));
  void shutdownGlobalThreading() override { shutdown_ = true; }
  MOCK_METHOD(void, shutdownThread, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  bool isShutdown() const override { return shutdown_; }

  SlotPtr allocateSlotMock() { return SlotPtr{new SlotImpl(*this, current_slot_++)}; }
  void runOnAllThreads1(std::function<void()> cb) { cb(); }
  void runOnAllThreads2(std::function<void()> cb, std::function<void()> main_callback) {
    cb();
    main_callback();
  }

  void setDispatcher(Event::Dispatcher* dispatcher) { dispatcher_ptr_ = dispatcher; }

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
      // The defer_delete mimics the slot being deleted on the main thread but the update not yet
      // getting to a worker.
      if (!parent_.shutdown_ && !parent_.defer_delete_) {
        EXPECT_LT(index_, parent_.data_.size());
        parent_.data_[index_].reset();
      }
    }

    // ThreadLocal::Slot
    ThreadLocalObjectSharedPtr get() override {
      EXPECT_TRUE(was_set_);
      return parent_.data_[index_];
    }
    bool currentThreadRegistered() override { return parent_.registered_; }
    void runOnAllThreads(const UpdateCb& cb) override {
      EXPECT_TRUE(was_set_);
      parent_.runOnAllThreads([cb, this]() { cb(parent_.data_[index_]); });
    }
    void runOnAllThreads(const UpdateCb& cb, const std::function<void()>& main_callback) override {
      EXPECT_TRUE(was_set_);
      parent_.runOnAllThreads([cb, this]() { cb(parent_.data_[index_]); }, main_callback);
    }
    bool isShutdown() const override { return parent_.shutdown_; }

    void set(InitializeCb cb) override {
      was_set_ = true;
      if (parent_.defer_data_) {
        parent_.deferred_data_[index_] = cb;
      } else {
        parent_.data_[index_] = cb(*parent_.dispatcher_ptr_);
      }
    }

    MockInstance& parent_;
    const uint32_t index_;
    bool was_set_{}; // set() must be called before other functions.
  };

  void call() {
    for (unsigned i = 0; i < deferred_data_.size(); i++) {
      data_[i] = deferred_data_[i](*dispatcher_ptr_);
    }
    deferred_data_.clear();
  }

  uint32_t current_slot_{};
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  Event::Dispatcher* dispatcher_ptr_ = &dispatcher_;
  std::vector<ThreadLocalObjectSharedPtr> data_;
  std::vector<Slot::InitializeCb> deferred_data_;
  bool defer_data_{};
  bool shutdown_{};
  bool registered_{true};
  bool defer_delete_{};
};

} // namespace ThreadLocal
} // namespace Envoy
