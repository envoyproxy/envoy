#pragma once

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>

#include "envoy/server/options.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/stats/stats_impl.h"

#include "server/server.h"
#include "server/test_hooks.h"

#include "test/test_common/utility.h"

namespace Server {

/**
 * Integration test options.
 */
class TestOptionsImpl : public Options {
public:
  TestOptionsImpl(const std::string& config_path)
      : config_path_(config_path), admin_address_path_("") {}

  // Server::Options
  uint64_t baseId() override { return 0; }
  uint32_t concurrency() override { return 1; }
  const std::string& configPath() override { return config_path_; }
  const std::string& adminAddressPath() override { return admin_address_path_; }
  std::chrono::seconds drainTime() override { return std::chrono::seconds(0); }
  spdlog::level::level_enum logLevel() override { NOT_IMPLEMENTED; }
  std::chrono::seconds parentShutdownTime() override { return std::chrono::seconds(0); }
  uint64_t restartEpoch() override { return 0; }
  std::chrono::milliseconds fileFlushIntervalMsec() override {
    return std::chrono::milliseconds(10000);
  }

private:
  const std::string config_path_;
  const std::string admin_address_path_;
};

class TestDrainManager : public DrainManager {
public:
  // Server::DrainManager
  bool drainClose() override { return draining_; }
  bool draining() override { return draining_; }
  void startDrainSequence() override {}
  void startParentShutdownSequence() override {}

  bool draining_{};
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
  std::list<CounterSharedPtr> counters() const override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.counters();
  }
  std::list<GaugeSharedPtr> gauges() const override {
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
  void shutdownThreading() override {}

private:
  mutable std::mutex lock_;
  IsolatedStoreImpl store_;
};

} // Stats

class IntegrationTestServer;
typedef std::unique_ptr<IntegrationTestServer> IntegrationTestServerPtr;

/**
 * Wrapper for running the real server for the purpose of integration tests.
 */
class IntegrationTestServer : Logger::Loggable<Logger::Id::testing>,
                              public TestHooks,
                              public Server::ComponentFactory {
public:
  static IntegrationTestServerPtr create(const std::string& config_path);
  ~IntegrationTestServer();

  Server::TestDrainManager& drainManager() { return *drain_manager_; }
  Server::InstanceImpl& server() {
    RELEASE_ASSERT(server_ != nullptr);
    return *server_;
  }
  void start();
  Stats::Store& store() { return stats_store_; }

  // TestHooks
  void onServerInitialized() override { server_initialized_.setReady(); }

  // Server::ComponentFactory
  Server::DrainManagerPtr createDrainManager(Server::Instance&) override {
    drain_manager_ = new Server::TestDrainManager();
    return Server::DrainManagerPtr{drain_manager_};
  }
  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override {
    return Server::InstanceUtil::createRuntime(server, config);
  }

protected:
  IntegrationTestServer(const std::string& config_path) : config_path_(config_path) {}

private:
  /**
   * Runs the real server on a thread.
   */
  void threadRoutine();

  const std::string config_path_;
  Thread::ThreadPtr thread_;
  ConditionalInitializer server_initialized_;
  ConditionalInitializer server_set_;
  std::unique_ptr<Server::InstanceImpl> server_;
  Server::TestDrainManager* drain_manager_{};
  Stats::TestIsolatedStoreImpl stats_store_;
};
