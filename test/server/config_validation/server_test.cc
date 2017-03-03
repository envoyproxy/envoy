#include "server/config_validation/hot_restart.h"
#include "server/config_validation/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

namespace Server {

// Test param is the path to the config file to validate.
class ValidationServerTest : public testing::TestWithParam<std::string> {
protected:
  ValidationServerTest()
      : options_(GetParam()),
        instance_(options_, restarter_, store_, access_log_lock_, component_factory_, local_info_) {
  }

  ~ValidationServerTest() { instance_.shutdown(); }

  testing::NiceMock<MockOptions> options_;
  ValidationHotRestart restarter_;
  Stats::TestIsolatedStoreImpl store_;
  Thread::MutexBasicLockable access_log_lock_;
  TestComponentFactory component_factory_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  ValidationInstance instance_;
};

TEST_P(ValidationServerTest, Foobar) {}

INSTANTIATE_TEST_CASE_P(ValidConfigs, ValidationServerTest,
                        ::testing::Values("configs/google_com_proxy.json",
                                          TestEnvironment::temporaryFileSubstitute(
                                              "test/config/integration/server.json",
                                              {{"upstream_0", 0}, {"upstream_1", 0}})));

} // Server
