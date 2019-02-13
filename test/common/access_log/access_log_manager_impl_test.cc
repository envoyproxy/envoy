#include <memory>

#include "common/access_log/access_log_manager_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace AccessLog {

TEST(AccessLogManagerImpl, reopenAllFiles) {
  Api::MockApi api;
  Filesystem::MockInstance file_system;
  EXPECT_CALL(api, fileSystem()).WillRepeatedly(ReturnRef(file_system));
  Event::MockDispatcher dispatcher;
  Thread::MutexBasicLockable lock;

  std::shared_ptr<Filesystem::MockFile> log1(new Filesystem::MockFile());
  std::shared_ptr<Filesystem::MockFile> log2(new Filesystem::MockFile());
  AccessLogManagerImpl access_log_manager(api, dispatcher, lock);
  EXPECT_CALL(file_system, createFile("foo", _, _)).WillOnce(Return(log1));
  access_log_manager.createAccessLog("foo");
  EXPECT_CALL(file_system, createFile("bar", _, _)).WillOnce(Return(log2));
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
