#include "test/integration/server.h"

#include <string>

#include "envoy/http/header_map.h"
#include "envoy/server/hot_restart.h"

#include "common/local_info/local_info_impl.h"
#include "common/network/utility.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
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
  void shutdown() override {}
  std::string version() override { return "1"; }
};

} // Server

IntegrationTestServerPtr IntegrationTestServer::create(const std::string& config_path,
                                                       const Network::Address::IpVersion version) {
  IntegrationTestServerPtr server{new IntegrationTestServer(config_path)};
  server->start(version);
  return server;
}

void IntegrationTestServer::start(const Network::Address::IpVersion version) {
  LOG(info, "starting integration test server");
  ASSERT(!thread_);
  thread_.reset(new Thread::Thread([version, this]() -> void { threadRoutine(version); }));
  // First, we want to wait until we know the server's worker threads are all
  // started.
  server_initialized_.waitReady();
  // Then we need to make sure the thread with
  // IntegrationTestServer::threadRoutine has set server_, since integration
  // tests might rely on the value of server().
  server_set_.waitReady();
}

IntegrationTestServer::~IntegrationTestServer() {
  LOG(info, "stopping integration test server");

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      server_->admin().socket().localAddress()->ip()->port(), "GET", "/quitquitquit", "",
      Http::CodecClient::Type::HTTP1, server_->admin().socket().localAddress()->ip()->version());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  thread_->join();
}

void IntegrationTestServer::threadRoutine(const Network::Address::IpVersion version) {
  Server::TestOptionsImpl options(config_path_);
  Server::TestHotRestart restarter;
  Thread::MutexBasicLockable lock;
  LocalInfo::LocalInfoImpl local_info(Network::Utility::getLocalAddress(version), "zone_name",
                                      "cluster_name", "node_name");
  server_.reset(
      new Server::InstanceImpl(options, *this, restarter, stats_store_, lock, *this, local_info));
  server_set_.setReady();
  server_->run();
  server_.reset();
}
} // Envoy
