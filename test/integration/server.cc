#include "integration.h"
#include "server.h"
#include "utility.h"

#include "envoy/http/header_map.h"
#include "envoy/server/hot_restart.h"

#include "common/tracing/http_tracer_impl.h"

namespace Server {

class TestHotRestart : public HotRestart {
public:
  // Server::HotRestart
  void drainParentListeners() override {}
  int duplicateParentListenSocket(uint32_t) override { return -1; }
  void getParentStats(GetParentStatsInfo& info) override { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) override {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) override {}
  void terminateParent() override {}
  std::string version() override { return "1"; }
};

} // Server

IntegrationTestServerPtr IntegrationTestServer::create(const std::string& config_path) {
  IntegrationTestServerPtr server{new IntegrationTestServer(config_path)};
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
  Stats::HeapRawStatDataAllocator stat_allocator;
  Stats::ThreadLocalStoreImpl stats_store(lock, stat_allocator);
  server_.reset(new Server::InstanceImpl(options, *this, restarter, stats_store, lock, *this));
  server_->run();
  server_.reset();
}
