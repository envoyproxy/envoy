#pragma once

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

namespace Envoy {
namespace Server {

/**
 * Integration test options.
 */
class TestOptionsImpl : public Options {
public:
  TestOptionsImpl(const std::string& config_path, Network::Address::IpVersion ip_version)
      : config_path_(config_path), local_address_ip_version_(ip_version),
        service_cluster_name_("cluster_name"), service_node_name_("node_name"),
        service_zone_("zone_name") {}

  // Server::Options
  uint64_t baseId() override { return 0; }
  uint32_t concurrency() override { return 1; }
  const std::string& configPath() override { return config_path_; }
  bool v2ConfigOnly() override { return false; }
  const std::string& adminAddressPath() override { return admin_address_path_; }
  Network::Address::IpVersion localAddressIpVersion() override { return local_address_ip_version_; }
  std::chrono::seconds drainTime() override { return std::chrono::seconds(1); }
  spdlog::level::level_enum logLevel() override { NOT_IMPLEMENTED; }
  std::chrono::seconds parentShutdownTime() override { return std::chrono::seconds(2); }
  const std::string& logPath() override { return log_path_; }
  uint64_t restartEpoch() override { return 0; }
  std::chrono::milliseconds fileFlushIntervalMsec() override {
    return std::chrono::milliseconds(50);
  }
  Mode mode() const override { return Mode::Serve; }
  const std::string& serviceClusterName() override { return service_cluster_name_; }
  const std::string& serviceNodeName() override { return service_node_name_; }
  const std::string& serviceZone() override { return service_zone_; }
  uint64_t maxStats() override { return 16384; }
  uint64_t maxObjNameLength() override { return 60; }

private:
  const std::string config_path_;
  const std::string admin_address_path_;
  const Network::Address::IpVersion local_address_ip_version_;
  const std::string service_cluster_name_;
  const std::string service_node_name_;
  const std::string service_zone_;
  const std::string log_path_;
};

class TestDrainManager : public DrainManager {
public:
  // Server::DrainManager
  bool drainClose() const override { return draining_; }
  void startDrainSequence(std::function<void()>) override {}
  void startParentShutdownSequence() override {}

  bool draining_{};
};

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

} // namespace Server

namespace Stats {

/**
 * This is a wrapper for Scopes for the TestIsolatedStoreImpl to ensure new scopes do
 * not interact with the store without grabbing the lock from TestIsolatedStoreImpl.
 */
class TestScopeWrapper : public Scope {
public:
  TestScopeWrapper(std::mutex& lock, ScopePtr wrapped_scope)
      : lock_(lock), wrapped_scope_(std::move(wrapped_scope)) {}

  ScopePtr createScope(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return ScopePtr{new TestScopeWrapper(lock_, wrapped_scope_->createScope(name))};
  }

  void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) override {
    std::unique_lock<std::mutex> lock(lock_);
    wrapped_scope_->deliverHistogramToSinks(histogram, value);
  }

  Counter& counter(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return wrapped_scope_->counter(name);
  }

  Gauge& gauge(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return wrapped_scope_->gauge(name);
  }

  Histogram& histogram(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return wrapped_scope_->histogram(name);
  }

private:
  std::mutex& lock_;
  ScopePtr wrapped_scope_;
};

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
  ScopePtr createScope(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return ScopePtr{new TestScopeWrapper(lock_, store_.createScope(name))};
  }
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gauge(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.gauge(name);
  }
  Histogram& histogram(const std::string& name) override {
    std::unique_lock<std::mutex> lock(lock_);
    return store_.histogram(name);
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

  // Stats::StoreRoot
  void addSink(Sink&) override {}
  void setTagProducer(TagProducerPtr&&) override {}
  void initializeThreading(Event::Dispatcher&, ThreadLocal::Instance&) override {}
  void shutdownThreading() override {}

private:
  mutable std::mutex lock_;
  IsolatedStoreImpl store_;
};

} // namespace Stats

class IntegrationTestServer;
typedef std::unique_ptr<IntegrationTestServer> IntegrationTestServerPtr;

/**
 * Wrapper for running the real server for the purpose of integration tests.
 */
class IntegrationTestServer : Logger::Loggable<Logger::Id::testing>,
                              public TestHooks,
                              public Server::ComponentFactory {
public:
  static IntegrationTestServerPtr create(const std::string& config_path,
                                         const Network::Address::IpVersion version,
                                         std::function<void()> pre_worker_start_test_steps);
  ~IntegrationTestServer();

  Server::TestDrainManager& drainManager() { return *drain_manager_; }
  Server::InstanceImpl& server() {
    RELEASE_ASSERT(server_ != nullptr);
    return *server_;
  }
  void setOnWorkerListenerAddedCb(std::function<void()> on_worker_listener_added) {
    on_worker_listener_added_cb_ = on_worker_listener_added;
  }
  void setOnWorkerListenerRemovedCb(std::function<void()> on_worker_listener_removed) {
    on_worker_listener_removed_cb_ = on_worker_listener_removed;
  }
  void start(const Network::Address::IpVersion version,
             std::function<void()> pre_worker_start_test_steps);
  void start();

  void waitForCounterGe(const std::string& name, uint64_t value) {
    while (counter(name) == nullptr || counter(name)->value() < value) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void waitForGaugeGe(const std::string& name, uint64_t value) {
    while (gauge(name) == nullptr || gauge(name)->value() < value) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void waitForGaugeEq(const std::string& name, uint64_t value) {
    while (gauge(name) == nullptr || gauge(name)->value() != value) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  Stats::CounterSharedPtr counter(const std::string& name) {
    // When using the thread local store, only counters() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findCounter(*stat_store_, name);
  }

  Stats::GaugeSharedPtr gauge(const std::string& name) {
    // When using the thread local store, only gauges() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findGauge(*stat_store_, name);
  }

  std::list<Stats::CounterSharedPtr> counters() { return stat_store_->counters(); }

  std::list<Stats::GaugeSharedPtr> gauges() { return stat_store_->gauges(); }

  // TestHooks
  void onWorkerListenerAdded() override;
  void onWorkerListenerRemoved() override;

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
  void threadRoutine(const Network::Address::IpVersion version);

  const std::string config_path_;
  Thread::ThreadPtr thread_;
  std::condition_variable listeners_cv_;
  std::mutex listeners_mutex_;
  uint64_t pending_listeners_;
  ConditionalInitializer server_set_;
  std::unique_ptr<Server::InstanceImpl> server_;
  Server::TestDrainManager* drain_manager_{};
  Stats::Store* stat_store_{};
  std::function<void()> on_worker_listener_added_cb_;
  std::function<void()> on_worker_listener_removed_cb_;
};

} // namespace Envoy
