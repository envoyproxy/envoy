#include "server/server.h"

#include "test/integration/server.h"
#include "test/mocks/common.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::InSequence;

namespace Server {

TEST(InitManagerImplTest, NoTargets) {
  InitManagerImpl manager;
  ReadyWatcher initialized;

  EXPECT_CALL(initialized, ready());
  manager.initialize([&]() -> void { initialized.ready(); });
}

TEST(InitManagerImplTest, Targets) {
  InSequence s;
  InitManagerImpl manager;
  Init::MockTarget target;
  ReadyWatcher initialized;

  manager.registerTarget(target);
  EXPECT_CALL(target, initialize(_));
  manager.initialize([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());
  target.callback_();
}

// Class creates minimally viable server instance for testing.
class ServerInstanceImplTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ServerInstanceImplTest()
      : options_(TestEnvironment::temporaryFileSubstitute("test/config/integration/server.json",
                                                          {{"upstream_0", 0}, {"upstream_1", 0}},
                                                          GetParam())),
        server_(options_, hooks_, restart_, stats_store_, fakelock_, component_factory_,
                local_info_) {}
  void TearDown() override {
    server_.clusterManager().shutdown();
    server_.threadLocal().shutdownThread();
  }

  testing::NiceMock<MockOptions> options_;
  DefaultTestHooks hooks_;
  testing::NiceMock<MockHotRestart> restart_;
  Stats::TestIsolatedStoreImpl stats_store_;
  Thread::MutexBasicLockable fakelock_;
  TestComponentFactory component_factory_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  InstanceImpl server_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, ServerInstanceImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ServerInstanceImplTest, NoListenSocketFds) {
  if (GetParam() == Network::Address::IpVersion::v4) {
    EXPECT_EQ(server_.getListenSocketFd("tcp://255.255.255.255:80"), -1);
  } else {
    EXPECT_EQ(server_.getListenSocketFd("tcp://[ff00::]:80"), -1);
  }
}

} // Server
} // Envoy
