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
  MOCK_METHOD(void, drainParentListeners, ());
  MOCK_METHOD(int, duplicateParentListenSocket,
              (const std::string& address, uint32_t worker_index));
  MOCK_METHOD(void, registerUdpForwardingListener,
              (Network::Address::InstanceConstSharedPtr address,
               std::shared_ptr<Network::UdpListenerConfig> listener_config));
  MOCK_METHOD(void, initialize, (Event::Dispatcher & dispatcher, Server::Instance& server));
  MOCK_METHOD(absl::optional<AdminShutdownResponse>, sendParentAdminShutdownRequest, ());
  MOCK_METHOD(void, sendParentTerminateRequest, ());
  MOCK_METHOD(ServerStatsFromParent, mergeParentStatsIfAny, (Stats::StoreRoot & stats_store));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(uint32_t, baseId, ());
  MOCK_METHOD(std::string, version, ());
  MOCK_METHOD(Thread::BasicLockable&, logLock, ());
  MOCK_METHOD(Thread::BasicLockable&, accessLogLock, ());
  MOCK_METHOD(Stats::Allocator&, statsAllocator, ());

private:
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable access_log_lock_;
  Stats::AllocatorImpl stats_allocator_;
};
} // namespace Server
} // namespace Envoy
