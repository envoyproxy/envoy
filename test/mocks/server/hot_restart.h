#pragma once

#include "envoy/server/instance.h"

#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockHotRestart : public HotRestart {
public:
  MockHotRestart();
  ~MockHotRestart() override;

  // Server::HotRestart
  MOCK_METHOD(void, drainParentListeners, (), (override));
  MOCK_METHOD(bool, parentStopAcceptingRequested, (), (override));
  MOCK_METHOD(int, duplicateParentListenSocket,
              (const std::string& address, uint32_t worker_index,
               absl::string_view network_namespace),
              (override));
  MOCK_METHOD(void, registerUdpForwardingListener,
              (Network::Address::InstanceConstSharedPtr address,
               std::shared_ptr<Network::UdpListenerConfig> listener_config),
              (override));
  MOCK_METHOD(OptRef<Network::ParentDrainedCallbackRegistrar>, parentDrainedCallbackRegistrar, (),
              (override));
  MOCK_METHOD(void, whenDrainComplete, (absl::string_view addr, absl::AnyInvocable<void()> action));
  MOCK_METHOD(void, initialize, (Event::Dispatcher & dispatcher, Server::Instance& server),
              (override));
  MOCK_METHOD(std::optional<AdminShutdownResponse>, sendParentAdminShutdownRequest, (), (override));
  MOCK_METHOD(void, sendParentTerminateRequest, (), (override));
  MOCK_METHOD(ServerStatsFromParent, mergeParentStatsIfAny, (Stats::StoreRoot & stats_store),
              (override));
  MOCK_METHOD(void, shutdown, (), (override));
  MOCK_METHOD(uint32_t, baseId, (), (override));
  MOCK_METHOD(std::string, version, (), (override));
  MOCK_METHOD(Thread::BasicLockable&, logLock, (), (override));
  MOCK_METHOD(Thread::BasicLockable&, accessLogLock, (), (override));
  MOCK_METHOD(bool, isInitializing, (), (const, override));
  MOCK_METHOD(Stats::Allocator&, statsAllocator, ());

private:
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable access_log_lock_;
  Stats::Allocator stats_allocator_;
};
} // namespace Server
} // namespace Envoy
