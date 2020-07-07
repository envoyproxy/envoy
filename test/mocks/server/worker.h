#pragma once

#include "envoy/server/worker.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockWorker : public Worker {
public:
  MockWorker();
  ~MockWorker() override;

  void callAddCompletion(bool success) {
    EXPECT_NE(nullptr, add_listener_completion_);
    add_listener_completion_(success);
    add_listener_completion_ = nullptr;
  }

  void callRemovalCompletion() {
    EXPECT_NE(nullptr, remove_listener_completion_);
    remove_listener_completion_();
    remove_listener_completion_ = nullptr;
  }

  void callDrainFilterChainsComplete() {
    EXPECT_NE(nullptr, remove_filter_chains_completion_);
    remove_filter_chains_completion_();
    remove_filter_chains_completion_ = nullptr;
  }

  // Server::Worker
  MOCK_METHOD(void, addListener,
              (absl::optional<uint64_t> overridden_listener, Network::ListenerConfig& listener,
               AddListenerCompletion completion));
  MOCK_METHOD(uint64_t, numConnections, (), (const));
  MOCK_METHOD(void, removeListener,
              (Network::ListenerConfig & listener, std::function<void()> completion));
  MOCK_METHOD(void, start, (GuardDog & guard_dog));
  MOCK_METHOD(void, initializeStats, (Stats::Scope & scope));
  MOCK_METHOD(void, stop, ());
  MOCK_METHOD(void, stopListener,
              (Network::ListenerConfig & listener, std::function<void()> completion));
  MOCK_METHOD(void, removeFilterChains,
              (uint64_t listener_tag, const std::list<const Network::FilterChain*>& filter_chains,
               std::function<void()> completion));

  AddListenerCompletion add_listener_completion_;
  std::function<void()> remove_listener_completion_;
  std::function<void()> remove_filter_chains_completion_;
};
} // namespace Server
} // namespace Envoy
