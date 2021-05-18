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
                        envoy::config::listener::v3::Listener_DrainType) -> DrainManagerSharedPtr {
            auto* manager = new NiceMock<MockDrainManager>();
            child_setup_(*manager);
            manager->child_setup_ = child_setup_;
            auto manager_ptr = std::shared_ptr<DrainManager>(manager);
            children_.push_back(std::weak_ptr<DrainManager>(manager_ptr));
            return manager_ptr;
          }));
  ON_CALL(*this, createChildManager(_))
      .WillByDefault(Invoke([this](Event::Dispatcher&) -> DrainManagerSharedPtr {
        auto* manager = new NiceMock<MockDrainManager>();
        child_setup_(*manager);
        manager->child_setup_ = child_setup_;
        auto manager_ptr = std::shared_ptr<DrainManager>(manager);
        children_.push_back(std::weak_ptr<DrainManager>(manager_ptr));
        return manager_ptr;
      }));

  ON_CALL(*this, drainClose()).WillByDefault(Invoke([this]() { return draining_.load(); }));
}

void MockDrainManager::startDrainSequence(std::function<void()> cb) {
  // Capture cb
  drain_sequence_completion_ = cb;

  // Proxy calls to children
  for (auto& child_weak_ptr : children_) {
    auto child = child_weak_ptr.lock();
    if (child) {
      child->startDrainSequence([] {});
    }
  }

  // flip internal drain state
  draining_ = true;

  // call the mock method to allow further assertions
  _startDrainSequence(cb);
}

MockDrainManager::~MockDrainManager() = default;

} // namespace Server
} // namespace Envoy
