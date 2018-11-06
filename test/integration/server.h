#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/server/options.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/stats/source_impl.h"

#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "test/integration/server_stats.h"
#include "test/integration/tcp_dump.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Server {

// Create OptionsImpl structures suitable for tests.
OptionsImpl createTestOptionsImpl(const std::string& config_path, const std::string& config_yaml,
                                  Network::Address::IpVersion ip_version);

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
  TestScopeWrapper(Thread::MutexBasicLockable& lock, ScopePtr wrapped_scope)
      : lock_(lock), wrapped_scope_(std::move(wrapped_scope)) {}

  ScopePtr createScope(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return ScopePtr{new TestScopeWrapper(lock_, wrapped_scope_->createScope(name))};
  }

  void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) override {
    Thread::LockGuard lock(lock_);
    wrapped_scope_->deliverHistogramToSinks(histogram, value);
  }

  Counter& counter(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->counter(name);
  }

  Gauge& gauge(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->gauge(name);
  }

  Histogram& histogram(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->histogram(name);
  }

  const StatsOptions& statsOptions() const override { return stats_options_; }

private:
  Thread::MutexBasicLockable& lock_;
  ScopePtr wrapped_scope_;
  StatsOptionsImpl stats_options_;
};

/**
 * This is a variant of the isolated store that has locking across all operations so that it can
 * be used during the integration tests.
 */
class TestIsolatedStoreImpl : public StoreRoot {
public:
  TestIsolatedStoreImpl() : source_(*this) {}
  // Stats::Scope
  Counter& counter(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return store_.counter(name);
  }
  ScopePtr createScope(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return ScopePtr{new TestScopeWrapper(lock_, store_.createScope(name))};
  }
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gauge(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return store_.gauge(name);
  }
  Histogram& histogram(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return store_.histogram(name);
  }
  const StatsOptions& statsOptions() const override { return stats_options_; }

  // Stats::Store
  std::vector<CounterSharedPtr> counters() const override {
    Thread::LockGuard lock(lock_);
    return store_.counters();
  }
  std::vector<GaugeSharedPtr> gauges() const override {
    Thread::LockGuard lock(lock_);
    return store_.gauges();
  }

  std::vector<ParentHistogramSharedPtr> histograms() const override {
    Thread::LockGuard lock(lock_);
    return store_.histograms();
  }

  // Stats::StoreRoot
  void addSink(Sink&) override {}
  void setTagProducer(TagProducerPtr&&) override {}
  void setStatsMatcher(StatsMatcherPtr&&) override {}
  void initializeThreading(Event::Dispatcher&, ThreadLocal::Instance&) override {}
  void shutdownThreading() override {}
  void mergeHistograms(PostMergeCb) override {}
  Source& source() override { return source_; }

private:
  mutable Thread::MutexBasicLockable lock_;
  IsolatedStoreImpl store_;
  SourceImpl source_;
  StatsOptionsImpl stats_options_;
};

} // namespace Stats

class IntegrationTestServer;
typedef std::unique_ptr<IntegrationTestServer> IntegrationTestServerPtr;

/**
 * Wrapper for running the real server for the purpose of integration tests.
 */
class IntegrationTestServer : Logger::Loggable<Logger::Id::testing>,
                              public TestHooks,
                              public IntegrationTestServerStats,
                              public Server::ComponentFactory {
public:
  static IntegrationTestServerPtr create(const std::string& config_path,
                                         const Network::Address::IpVersion version,
                                         std::function<void()> pre_worker_start_test_steps,
                                         bool deterministic, Event::TestTimeSystem& time_system);
  ~IntegrationTestServer();

  Server::TestDrainManager& drainManager() { return *drain_manager_; }
  Server::InstanceImpl& server() {
    RELEASE_ASSERT(server_ != nullptr, "");
    return *server_;
  }
  void setOnWorkerListenerAddedCb(std::function<void()> on_worker_listener_added) {
    on_worker_listener_added_cb_ = on_worker_listener_added;
  }
  void setOnWorkerListenerRemovedCb(std::function<void()> on_worker_listener_removed) {
    on_worker_listener_removed_cb_ = on_worker_listener_removed;
  }
  void start(const Network::Address::IpVersion version,
             std::function<void()> pre_worker_start_test_steps, bool deterministic);

  void waitForCounterGe(const std::string& name, uint64_t value) override {
    while (counter(name) == nullptr || counter(name)->value() < value) {
      time_system_.sleep(std::chrono::milliseconds(10));
    }
  }

  void waitForGaugeGe(const std::string& name, uint64_t value) override {
    while (gauge(name) == nullptr || gauge(name)->value() < value) {
      time_system_.sleep(std::chrono::milliseconds(10));
    }
  }

  void waitForGaugeEq(const std::string& name, uint64_t value) override {
    while (gauge(name) == nullptr || gauge(name)->value() != value) {
      time_system_.sleep(std::chrono::milliseconds(10));
    }
  }

  Stats::CounterSharedPtr counter(const std::string& name) override {
    // When using the thread local store, only counters() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findCounter(*stat_store_, name);
  }

  Stats::GaugeSharedPtr gauge(const std::string& name) override {
    // When using the thread local store, only gauges() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findGauge(*stat_store_, name);
  }

  std::vector<Stats::CounterSharedPtr> counters() override { return stat_store_->counters(); }

  std::vector<Stats::GaugeSharedPtr> gauges() override { return stat_store_->gauges(); }

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
  IntegrationTestServer(Event::TestTimeSystem& time_system, const std::string& config_path)
      : time_system_(time_system), config_path_(config_path) {}

private:
  /**
   * Runs the real server on a thread.
   */
  void threadRoutine(const Network::Address::IpVersion version, bool deterministic);

  Event::TestTimeSystem& time_system_;
  const std::string config_path_;
  Thread::ThreadPtr thread_;
  Thread::CondVar listeners_cv_;
  Thread::MutexBasicLockable listeners_mutex_;
  uint64_t pending_listeners_;
  ConditionalInitializer server_set_;
  std::unique_ptr<Server::InstanceImpl> server_;
  Server::TestDrainManager* drain_manager_{};
  Stats::Store* stat_store_{};
  std::function<void()> on_worker_listener_added_cb_;
  std::function<void()> on_worker_listener_removed_cb_;
  Network::Address::InstanceConstSharedPtr admin_address_;
  TcpDumpPtr tcp_dump_;
};

} // namespace Envoy
