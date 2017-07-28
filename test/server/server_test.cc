#include "common/thread_local/thread_local_impl.h"

#include "server/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

// Class creates minimally viable server instance for testing.
class ServerInstanceImplTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ServerInstanceImplTest()
      : version_(GetParam()),
        options_(TestEnvironment::temporaryFileSubstitute("test/config/integration/server.json",
                                                          {{"upstream_0", 0}, {"upstream_1", 0}},
                                                          version_)),
        server_(options_, hooks_, restart_, stats_store_, fakelock_, component_factory_,
                local_info_, thread_local_) {}
  void TearDown() override {
    server_.threadLocal().shutdownGlobalThreading();
    server_.clusterManager().shutdown();
    server_.threadLocal().shutdownThread();
  }

  Network::Address::IpVersion version_;
  testing::NiceMock<MockOptions> options_;
  DefaultTestHooks hooks_;
  testing::NiceMock<MockHotRestart> restart_;
  ThreadLocal::InstanceImpl thread_local_;
  Stats::TestIsolatedStoreImpl stats_store_;
  Thread::MutexBasicLockable fakelock_;
  TestComponentFactory component_factory_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  InstanceImpl server_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, ServerInstanceImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ServerInstanceImplTest, Stats) {
  EXPECT_NE(nullptr, TestUtility::findCounter(stats_store_, "server.watchdog_miss"));
}

} // namespace Server
} // namespace Envoy
