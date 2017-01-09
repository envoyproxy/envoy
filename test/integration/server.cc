#include "integration.h"
#include "server.h"
#include "utility.h"

#include "envoy/http/header_map.h"
#include "envoy/server/hot_restart.h"

#include "common/local_info/local_info_impl.h"

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

namespace Stats {

/**
 * This is a variant of the isolated store that has locking across all operations so that it can
 * be used during the integration tests.
 */
class TestIsolatedStoreImpl : public StoreRoot {
public:
  // Stats::Scope
  Counter& counter(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.counter(name);
  }
  void deliverHistogramToSinks(const std::string&, uint64_t) override {}
  void deliverTimingToSinks(const std::string&, std::chrono::milliseconds) override {}
  Gauge& gauge(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.gauge(name);
  }
  Timer& timer(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.timer(name);
  }

  // Stats::Store
  std::list<CounterPtr> counters() const override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.counters();
  }
  std::list<GaugePtr> gauges() const override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.gauges();
  }
  ScopePtr createScope(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.createScope(name);
  }

  // Stats::StoreRoot
  void addSink(Sink&) override {}
  void initializeThreading(Event::Dispatcher&, ThreadLocal::Instance&) override {}

private:
  mutable std::mutex lock_;
  IsolatedStoreImpl store_;
};

} // Stats

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
  Stats::TestIsolatedStoreImpl stats_store;
  LocalInfo::LocalInfoImpl local_info("127.0.0.1", "zone_name", "cluster_name", "node_name");
  server_.reset(
      new Server::InstanceImpl(options, *this, restarter, stats_store, lock, *this, local_info));
  server_->run();
  server_.reset();
}
