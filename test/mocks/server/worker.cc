#include "worker.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::Invoke;

MockWorker::MockWorker() {
  ON_CALL(*this, addListener(_, _, _, _))
      .WillByDefault(Invoke([this](absl::optional<uint64_t> overridden_listener,
                                   Network::ListenerConfig& config,
                                   AddListenerCompletion completion, Runtime::Loader&) -> void {
        UNREFERENCED_PARAMETER(overridden_listener);
        config.listenSocketFactories()[0]->getListenSocket(0);
        EXPECT_EQ(nullptr, add_listener_completion_);
        add_listener_completion_ = completion;
      }));

  ON_CALL(*this, removeListener(_, _))
      .WillByDefault(
          Invoke([this](Network::ListenerConfig&, std::function<void()> completion) -> void {
            EXPECT_EQ(nullptr, remove_listener_completion_);
            remove_listener_completion_ = completion;
          }));

  ON_CALL(*this, stopListener(_, _))
      .WillByDefault(Invoke([](Network::ListenerConfig&, std::function<void()> completion) -> void {
        if (completion != nullptr) {
          completion();
        }
      }));

  ON_CALL(*this, removeFilterChains(_, _, _))
      .WillByDefault(Invoke([this](uint64_t, const std::list<const Network::FilterChain*>&,
                                   std::function<void()> completion) -> void {
        EXPECT_EQ(nullptr, remove_filter_chains_completion_);
        remove_filter_chains_completion_ = completion;
      }));

  ON_CALL(*this, start(_, _)).WillByDefault(Invoke([](GuardDog&, const Event::PostCb& cb) -> void {
    cb();
  }));
}

MockWorker::~MockWorker() = default;

} // namespace Server
} // namespace Envoy
