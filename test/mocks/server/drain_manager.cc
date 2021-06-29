#include "drain_manager.h"

#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/server/drain_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

MockDrainManager::MockDrainManager() {
  ON_CALL(*this, createChildManager(_, _))
      .WillByDefault(
          Invoke([this](Event::Dispatcher&,
                        envoy::config::listener::v3::Listener_DrainType) -> DrainManagerPtr {
            auto* manager = new NiceMock<MockDrainManager>();
            child_setup_(*manager);
            manager->child_setup_ = child_setup_;
            manager->parent_alive_ = std::weak_ptr<bool>(this->still_alive_);
            manager->parent_ = this;
            children_.push_back(manager);
            return std::unique_ptr<DrainManager>(manager);
          }));
  ON_CALL(*this, createChildManager(_))
      .WillByDefault(Invoke([this](Event::Dispatcher&) -> DrainManagerPtr {
        auto* manager = new NiceMock<MockDrainManager>();
        child_setup_(*manager);
        manager->child_setup_ = child_setup_;
        manager->parent_alive_ = std::weak_ptr<bool>(this->still_alive_);
        manager->parent_ = this;
        children_.push_back(manager);
        return std::unique_ptr<DrainManager>(manager);
      }));

  ON_CALL(*this, drainClose()).WillByDefault(Invoke([this]() { return draining_.load(); }));
}

void MockDrainManager::startDrainSequence(std::function<void()> cb) {
  // Capture cb
  drain_sequence_completion_ = cb;

  // Proxy calls to children
  for (auto& child : children_) {
    child->startDrainSequence([] {});
  }

  // flip internal drain state
  draining_ = true;

  // call the mock method to allow further assertions
  _startDrainSequence(cb);
}

MockDrainManager::~MockDrainManager() {
  if (!parent_alive_.expired() && parent_) {
    auto child_it = std::find(parent_->children_.begin(), parent_->children_.end(), this);
    if (child_it != parent_->children_.end()) {
      parent_->children_.erase(child_it);
    }
  }
};

} // namespace Server
} // namespace Envoy
