#include "hot_restart.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::ReturnRef;

MockHotRestart::MockHotRestart() : stats_allocator_(*symbol_table_) {
  ON_CALL(*this, logLock()).WillByDefault(ReturnRef(log_lock_));
  ON_CALL(*this, accessLogLock()).WillByDefault(ReturnRef(access_log_lock_));
  ON_CALL(*this, statsAllocator()).WillByDefault(ReturnRef(stats_allocator_));
}

MockHotRestart::~MockHotRestart() = default;

} // namespace Server
} // namespace Envoy
