#include "test/integration/server.h"

#include "envoy/http/header_map.h"
#include "envoy/server/hot_restart.h"

#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"

namespace Server {

class TestHotRestart : public HotRestart {
public:
  // Server::HotRestart
  void drainParentListeners() override {}
  int duplicateParentListenSocket(const std::string&) override { return -1; }
  void getParentStats(GetParentStatsInfo& info) override { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) override {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) override {}
  void terminateParent() override {}
  std::string version() override { return "1"; }
};

} // Server

IntegrationTestServerPtr IntegrationTestServer::create(const std::string& config_path) {
  IntegrationTestServerPtr server{
      new IntegrationTestServer(TestEnvironment::temporaryPath(config_path))};
  server->start();
  return server;
}

void IntegrationTestServer::start() {
  log().info("starting integration test server");
  ASSERT(!thread_);
  thread_.reset(new Thread::Thread([this]() -> void { threadRoutine(); }));
  server_initialized_.waitReady();
}

IntegrationTestServer::~IntegrationTestServer() {
  log().info("stopping integration test server");

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      IntegrationTest::ADMIN_PORT, "GET", "/quitquitquit", "", Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  thread_->join();
}

void IntegrationTestServer::threadRoutine() {
  Server::TestOptionsImpl options(config_path_);
  Server::TestHotRestart restarter;
  Thread::MutexBasicLockable lock;
  LocalInfo::LocalInfoImpl local_info(Network::Utility::getLocalAddress(), "zone_name",
                                      "cluster_name", "node_name");
  server_.reset(
      new Server::InstanceImpl(options, *this, restarter, stats_store_, lock, *this, local_info));
  server_->run();
  server_.reset();
}
