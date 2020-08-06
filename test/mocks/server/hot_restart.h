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
  MOCK_METHOD(int, duplicateParentListenSocket, (const std::string& address));
  MOCK_METHOD(std::unique_ptr<envoy::HotRestartMessage>, getParentStats, ());
  MOCK_METHOD(void, initialize, (Event::Dispatcher & dispatcher, Server::Instance& server));
  MOCK_METHOD(void, sendParentAdminShutdownRequest, (time_t & original_start_time));
  MOCK_METHOD(void, sendParentTerminateRequest, ());
  MOCK_METHOD(ServerStatsFromParent, mergeParentStatsIfAny, (Stats::StoreRoot & stats_store));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(uint32_t, baseId, ());
  MOCK_METHOD(std::string, version, ());
  MOCK_METHOD(Thread::BasicLockable&, logLock, ());
  MOCK_METHOD(Thread::BasicLockable&, accessLogLock, ());
  MOCK_METHOD(Stats::Allocator&, statsAllocator, ());

private:
  Stats::TestSymbolTable symbol_table_;
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable access_log_lock_;
  Stats::AllocatorImpl stats_allocator_;
};
} // namespace Server
} // namespace Envoy
