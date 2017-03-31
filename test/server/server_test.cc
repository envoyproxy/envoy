#include "server/server.h"

#include "test/integration/server.h"
#include "test/mocks/common.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

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

class TestComponentFactory : public ComponentFactory {
public:
  Server::DrainManagerPtr createDrainManager(Server::Instance&) override {
    return Server::DrainManagerPtr{new Server::TestDrainManager()};
  }
  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override {
    return Server::InstanceUtil::createRuntime(server, config);
  }
};

// Class creates minimally viable server instance for testing.
class ServerInstanceImplTest : public testing::Test {
protected:
  ServerInstanceImplTest()
      : options_(TestEnvironment::temporaryFileSubstitutePorts(
            "test/config/integration/server.json", {{"upstream_0", 0}, {"upstream_1", 0}})),
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

TEST_F(ServerInstanceImplTest, NoListenSocketFds) {
  EXPECT_EQ(server_.getListenSocketFd("tcp://255.255.255.255:80"), -1);
}

} // Server
