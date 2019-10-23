#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/server/options.h"
#include "envoy/server/process_context.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/thread.h"

#include "server/listener_hooks.h"
#include "server/options_impl.h"
#include "server/server.h"

#include "test/integration/server_stats.h"
#include "test/integration/tcp_dump.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

// Create OptionsImpl structures suitable for tests.
OptionsImpl createTestOptionsImpl(const std::string& config_path, const std::string& config_yaml,
                                  Network::Address::IpVersion ip_version,
                                  bool allow_unknown_static_fields = false,
                                  bool reject_unknown_dynamic_fields = false,
                                  uint32_t concurrency = 1);

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

  Counter& counterFromStatName(StatName name) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->counterFromStatName(name);
  }

  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->gaugeFromStatName(name, import_mode);
  }

  Histogram& histogramFromStatName(StatName name, Histogram::Unit unit) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->histogramFromStatName(name, unit);
  }
  NullGaugeImpl& nullGauge(const std::string& str) override {
    return wrapped_scope_->nullGauge(str);
  }

  Counter& counter(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return counterFromStatName(storage.statName());
  }
  Gauge& gauge(const std::string& name, Gauge::ImportMode import_mode) override {
    StatNameManagedStorage storage(name, symbolTable());
    return gaugeFromStatName(storage.statName(), import_mode);
  }
  Histogram& histogram(const std::string& name, Histogram::Unit unit) override {
    StatNameManagedStorage storage(name, symbolTable());
    return histogramFromStatName(storage.statName(), unit);
  }

  OptionalCounter findCounter(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->findCounter(name);
  }
  OptionalGauge findGauge(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->findGauge(name);
  }
  OptionalHistogram findHistogram(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->findHistogram(name);
  }

  const SymbolTable& constSymbolTable() const override {
    return wrapped_scope_->constSymbolTable();
  }
  SymbolTable& symbolTable() override { return wrapped_scope_->symbolTable(); }

private:
  Thread::MutexBasicLockable& lock_;
  ScopePtr wrapped_scope_;
};

/**
 * This is a variant of the isolated store that has locking across all operations so that it can
 * be used during the integration tests.
 */
class TestIsolatedStoreImpl : public StoreRoot {
public:
  // Stats::Scope
  Counter& counterFromStatName(StatName name) override {
    Thread::LockGuard lock(lock_);
    return store_.counterFromStatName(name);
  }
  Counter& counter(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return store_.counter(name);
  }
  ScopePtr createScope(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return ScopePtr{new TestScopeWrapper(lock_, store_.createScope(name))};
  }
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override {
    Thread::LockGuard lock(lock_);
    return store_.gaugeFromStatName(name, import_mode);
  }
  Gauge& gauge(const std::string& name, Gauge::ImportMode import_mode) override {
    Thread::LockGuard lock(lock_);
    return store_.gauge(name, import_mode);
  }
  Histogram& histogramFromStatName(StatName name, Histogram::Unit unit) override {
    Thread::LockGuard lock(lock_);
    return store_.histogramFromStatName(name, unit);
  }
  NullGaugeImpl& nullGauge(const std::string& name) override { return store_.nullGauge(name); }
  Histogram& histogram(const std::string& name, Histogram::Unit unit) override {
    Thread::LockGuard lock(lock_);
    return store_.histogram(name, unit);
  }
  OptionalCounter findCounter(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return store_.findCounter(name);
  }
  OptionalGauge findGauge(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return store_.findGauge(name);
  }
  OptionalHistogram findHistogram(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return store_.findHistogram(name);
  }
  const SymbolTable& constSymbolTable() const override { return store_.constSymbolTable(); }
  SymbolTable& symbolTable() override { return store_.symbolTable(); }

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

private:
  mutable Thread::MutexBasicLockable lock_;
  IsolatedStoreImpl store_;
};

} // namespace Stats

class IntegrationTestServer;
using IntegrationTestServerPtr = std::unique_ptr<IntegrationTestServer>;

/**
 * Wrapper for running the real server for the purpose of integration tests.
 * This class is an Abstract Base Class and delegates ownership and management
 * of the actual envoy server to a derived class. See the documentation for
 * createAndRunEnvoyServer().
 */
class IntegrationTestServer : public Logger::Loggable<Logger::Id::testing>,
                              public ListenerHooks,
                              public IntegrationTestServerStats,
                              public Server::ComponentFactory {
public:
  static IntegrationTestServerPtr
  create(const std::string& config_path, const Network::Address::IpVersion version,
         std::function<void()> on_server_init_function, bool deterministic,
         Event::TestTimeSystem& time_system, Api::Api& api,
         bool defer_listener_finalization = false,
         absl::optional<std::reference_wrapper<ProcessObject>> process_object = absl::nullopt,
         bool allow_unknown_static_fields = false, bool reject_unknown_dynamic_fields = false,
         uint32_t concurrency = 1);
  // Note that the derived class is responsible for tearing down the server in its
  // destructor.
  ~IntegrationTestServer() override;

  void waitUntilListenersReady();

  Server::TestDrainManager& drainManager() { return *drain_manager_; }
  void setOnWorkerListenerAddedCb(std::function<void()> on_worker_listener_added) {
    on_worker_listener_added_cb_ = std::move(on_worker_listener_added);
  }
  void setOnWorkerListenerRemovedCb(std::function<void()> on_worker_listener_removed) {
    on_worker_listener_removed_cb_ = std::move(on_worker_listener_removed);
  }
  void onRuntimeCreated() override;

  void start(const Network::Address::IpVersion version,
             std::function<void()> on_server_init_function, bool deterministic,
             bool defer_listener_finalization,
             absl::optional<std::reference_wrapper<ProcessObject>> process_object,
             bool allow_unknown_static_fields, bool reject_unknown_dynamic_fields,
             uint32_t concurrency);

  void waitForCounterEq(const std::string& name, uint64_t value) override {
    TestUtility::waitForCounterEq(stat_store(), name, value, time_system_);
  }

  void waitForCounterGe(const std::string& name, uint64_t value) override {
    TestUtility::waitForCounterGe(stat_store(), name, value, time_system_);
  }

  void waitForGaugeGe(const std::string& name, uint64_t value) override {
    TestUtility::waitForGaugeGe(stat_store(), name, value, time_system_);
  }

  void waitForGaugeEq(const std::string& name, uint64_t value) override {
    TestUtility::waitForGaugeEq(stat_store(), name, value, time_system_);
  }

  Stats::CounterSharedPtr counter(const std::string& name) override {
    // When using the thread local store, only counters() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findCounter(stat_store(), name);
  }

  Stats::GaugeSharedPtr gauge(const std::string& name) override {
    // When using the thread local store, only gauges() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findGauge(stat_store(), name);
  }

  std::vector<Stats::CounterSharedPtr> counters() override { return stat_store().counters(); }

  std::vector<Stats::GaugeSharedPtr> gauges() override { return stat_store().gauges(); }

  // ListenerHooks
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

  // Should not be called until createAndRunEnvoyServer() is called.
  virtual Server::Instance& server() PURE;
  virtual Stats::Store& stat_store() PURE;
  virtual Network::Address::InstanceConstSharedPtr admin_address() PURE;

protected:
  IntegrationTestServer(Event::TestTimeSystem& time_system, Api::Api& api,
                        const std::string& config_path)
      : time_system_(time_system), api_(api), config_path_(config_path) {}

  // Create the running envoy server. This function will call serverReady() when the virtual
  // functions server(), stat_store(), and admin_address() may be called, but before the server
  // has been started.
  // The subclass is also responsible for tearing down this server in its destructor.
  virtual void createAndRunEnvoyServer(
      OptionsImpl& options, Event::TimeSystem& time_system,
      Network::Address::InstanceConstSharedPtr local_address, ListenerHooks& hooks,
      Thread::BasicLockable& access_log_lock, Server::ComponentFactory& component_factory,
      Runtime::RandomGeneratorPtr&& random_generator,
      absl::optional<std::reference_wrapper<ProcessObject>> process_object) PURE;

  // Will be called by subclass on server thread when the server is ready to be accessed. The
  // server may not have been run yet, but all server access methods (server(), stat_store(),
  // adminAddress()) will be available.
  void serverReady();

private:
  /**
   * Runs the real server on a thread.
   */
  void threadRoutine(const Network::Address::IpVersion version, bool deterministic,
                     absl::optional<std::reference_wrapper<ProcessObject>> process_object,
                     bool allow_unknown_static_fields, bool reject_unknown_dynamic_fields,
                     uint32_t concurrency);

  Event::TestTimeSystem& time_system_;
  Api::Api& api_;
  const std::string config_path_;
  Thread::ThreadPtr thread_;
  Thread::CondVar listeners_cv_;
  Thread::MutexBasicLockable listeners_mutex_;
  uint64_t pending_listeners_;
  ConditionalInitializer server_set_;
  Server::TestDrainManager* drain_manager_{};
  std::function<void()> on_worker_listener_added_cb_;
  std::function<void()> on_worker_listener_removed_cb_;
  TcpDumpPtr tcp_dump_;
};

// Default implementation of IntegrationTestServer
class IntegrationTestServerImpl : public IntegrationTestServer {
public:
  IntegrationTestServerImpl(Event::TestTimeSystem& time_system, Api::Api& api,
                            const std::string& config_path)
      : IntegrationTestServer(time_system, api, config_path) {}

  ~IntegrationTestServerImpl() override;

  Server::Instance& server() override {
    RELEASE_ASSERT(server_ != nullptr, "");
    return *server_;
  }
  Stats::Store& stat_store() override {
    RELEASE_ASSERT(stat_store_ != nullptr, "");
    return *stat_store_;
  }
  Network::Address::InstanceConstSharedPtr admin_address() override { return admin_address_; }

private:
  void createAndRunEnvoyServer(
      OptionsImpl& options, Event::TimeSystem& time_system,
      Network::Address::InstanceConstSharedPtr local_address, ListenerHooks& hooks,
      Thread::BasicLockable& access_log_lock, Server::ComponentFactory& component_factory,
      Runtime::RandomGeneratorPtr&& random_generator,
      absl::optional<std::reference_wrapper<ProcessObject>> process_object) override;

  // Owned by this class. An owning pointer is not used because the actual allocation is done
  // on a stack in a non-main thread.
  Server::Instance* server_{};
  Stats::Store* stat_store_{};
  Network::Address::InstanceConstSharedPtr admin_address_;
  absl::Notification server_gone_;
};

} // namespace Envoy
