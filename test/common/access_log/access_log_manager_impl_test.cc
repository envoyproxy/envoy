#include <memory>

#include "common/access_log/access_log_manager_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace AccessLog {

TEST(AccessLogManagerImpl, reopenAllFiles) {
  Api::MockApi api;
  Event::MockDispatcher dispatcher;
  Thread::MutexBasicLockable lock;
  Stats::IsolatedStoreImpl stats_store;

  std::shared_ptr<Filesystem::MockFile> log1(new Filesystem::MockFile());
  std::shared_ptr<Filesystem::MockFile> log2(new Filesystem::MockFile());
  AccessLogManagerImpl access_log_manager(api, dispatcher, lock);
  EXPECT_CALL(api, createFile("foo", _, _)).WillOnce(Return(log1));
  access_log_manager.createAccessLog("foo");
  EXPECT_CALL(api, createFile("bar", _, _)).WillOnce(Return(log2));
  access_log_manager.createAccessLog("bar");

  // Make sure that getting the access log with the same name returns the same underlying file.
  EXPECT_EQ(log1, access_log_manager.createAccessLog("foo"));
  EXPECT_EQ(log2, access_log_manager.createAccessLog("bar"));

  EXPECT_CALL(*log1, reopen());
  EXPECT_CALL(*log2, reopen());
  access_log_manager.reopen();
}

} // namespace AccessLog
} // namespace Envoy
