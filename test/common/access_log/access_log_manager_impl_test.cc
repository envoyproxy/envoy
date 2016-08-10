#include "common/access_log/access_log_manager.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"

using testing::_;

namespace AccessLog {

TEST(AccessLogManagerImpl, reopenAllFiles) {
  AccessLogManagerImpl access_log_manager;
  MockAccessLog* log1 = new MockAccessLog();
  MockAccessLog* log2 = new MockAccessLog();

  // Register access log in access log manager
  access_log_manager.registerAccessLog(std::shared_ptr<MockAccessLog>(log1));
  access_log_manager.registerAccessLog(std::shared_ptr<MockAccessLog>(log2));

  EXPECT_CALL(*log1, reopen());
  EXPECT_CALL(*log2, reopen());

  access_log_manager.reopen();
}

} // AccessLog
