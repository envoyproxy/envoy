#include "server/server.h"

#include "test/mocks/common.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "test/integration/server.h" // for TestDrainManager, TestIsolatedStoreImpl

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
      : options_(std::string("test/config/integration/server.json")),
        server_(options_, hooks_, restart_, stats_store_, fakelock_, component_factory_,
                local_info_) {}
  void TearDown() {
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
  std::string bad_tcp_url("tcp://255.255.255.255:80");
  EXPECT_EQ(server_.getListenSocketFd(bad_tcp_url), -1);
}

} // Server
