#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/server/options.h"
#include "envoy/server/process_context.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/stats/allocator_impl.h"
#include "source/server/drain_manager_impl.h"
#include "source/server/listener_hooks.h"
#include "source/server/options_impl_base.h"
#include "source/server/server.h"

#include "test/integration/server_stats.h"
#include "test/integration/tcp_dump.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

struct FieldValidationConfig {
  bool allow_unknown_static_fields = false;
  bool reject_unknown_dynamic_fields = false;
  bool ignore_unknown_dynamic_fields = false;
};

// Create OptionsImplBase structures suitable for tests. Disables hot restart.
OptionsImplBase createTestOptionsImpl(
    const std::string& config_path, const std::string& config_yaml,
    Network::Address::IpVersion ip_version,
    FieldValidationConfig validation_config = FieldValidationConfig(), uint32_t concurrency = 1,
    std::chrono::seconds drain_time = std::chrono::seconds(1),
    Server::DrainStrategy drain_strategy = Server::DrainStrategy::Gradual,
    bool use_bootstrap_node_metadata = false,
    std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>&& config_proto = nullptr);

class TestComponentFactory : public ComponentFactory {
public:
  Server::DrainManagerPtr createDrainManager(Server::Instance& server) override {
    return Server::DrainManagerPtr{new Server::DrainManagerImpl(
        server, envoy::config::listener::v3::Listener::MODIFY_ONLY, server.dispatcher())};
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
  TestScopeWrapper(Thread::MutexBasicLockable& lock, ScopeSharedPtr wrapped_scope, Store& store)
      : lock_(lock), wrapped_scope_(wrapped_scope), store_(store) {}

  ScopeSharedPtr createScope(const std::string& name) override {
    Thread::LockGuard lock(lock_);
    return std::make_shared<TestScopeWrapper>(lock_, wrapped_scope_->createScope(name), store_);
  }

  ScopeSharedPtr scopeFromStatName(StatName name) override {
    Thread::LockGuard lock(lock_);
    return std::make_shared<TestScopeWrapper>(lock_, wrapped_scope_->scopeFromStatName(name),
                                              store_);
  }

  Counter& counterFromStatNameWithTags(const StatName& name,
                                       StatNameTagVectorOptConstRef tags) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->counterFromStatNameWithTags(name, tags);
  }

  Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                   Gauge::ImportMode import_mode) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->gaugeFromStatNameWithTags(name, tags, import_mode);
  }

  Histogram& histogramFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                           Histogram::Unit unit) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->histogramFromStatNameWithTags(name, tags, unit);
  }

  TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                               StatNameTagVectorOptConstRef tags) override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->textReadoutFromStatNameWithTags(name, tags);
  }

  Counter& counterFromString(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return counterFromStatName(storage.statName());
  }
  Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) override {
    StatNameManagedStorage storage(name, symbolTable());
    return gaugeFromStatName(storage.statName(), import_mode);
  }
  Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) override {
    StatNameManagedStorage storage(name, symbolTable());
    return histogramFromStatName(storage.statName(), unit);
  }
  TextReadout& textReadoutFromString(const std::string& name) override {
    StatNameManagedStorage storage(name, symbolTable());
    return textReadoutFromStatName(storage.statName());
  }

  CounterOptConstRef findCounter(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->findCounter(name);
  }
  GaugeOptConstRef findGauge(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->findGauge(name);
  }
  HistogramOptConstRef findHistogram(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->findHistogram(name);
  }
  TextReadoutOptConstRef findTextReadout(StatName name) const override {
    Thread::LockGuard lock(lock_);
    return wrapped_scope_->findTextReadout(name);
  }

  const SymbolTable& constSymbolTable() const override {
    return wrapped_scope_->constSymbolTable();
  }
  SymbolTable& symbolTable() override { return wrapped_scope_->symbolTable(); }

  bool iterate(const IterateFn<Counter>& fn) const override { return wrapped_scope_->iterate(fn); }
  bool iterate(const IterateFn<Gauge>& fn) const override { return wrapped_scope_->iterate(fn); }
  bool iterate(const IterateFn<Histogram>& fn) const override {
    return wrapped_scope_->iterate(fn);
  }
  bool iterate(const IterateFn<TextReadout>& fn) const override {
    return wrapped_scope_->iterate(fn);
  }
  StatName prefix() const override { return wrapped_scope_->prefix(); }
  Store& store() override { return store_; }
  const Store& constStore() const override { return store_; }

private:
  Thread::MutexBasicLockable& lock_;
  ScopeSharedPtr wrapped_scope_;
  Store& store_;
};

// A counter which signals on a condition variable when it is incremented.
class NotifyingCounter : public Stats::Counter {
public:
  NotifyingCounter(Stats::Counter* counter, absl::Mutex& mutex, absl::CondVar& condvar)
      : counter_(counter), mutex_(mutex), condvar_(condvar) {}

  std::string name() const override { return counter_->name(); }
  StatName statName() const override { return counter_->statName(); }
  TagVector tags() const override { return counter_->tags(); }
  std::string tagExtractedName() const override { return counter_->tagExtractedName(); }
  void iterateTagStatNames(const TagStatNameIterFn& fn) const override {
    counter_->iterateTagStatNames(fn);
  }
  void add(uint64_t amount) override {
    counter_->add(amount);
    absl::MutexLock l(&mutex_);
    condvar_.Signal();
  }
  void inc() override { add(1); }
  uint64_t latch() override { return counter_->latch(); }
  void reset() override { return counter_->reset(); }
  uint64_t value() const override { return counter_->value(); }
  void incRefCount() override { counter_->incRefCount(); }
  bool decRefCount() override { return counter_->decRefCount(); }
  uint32_t use_count() const override { return counter_->use_count(); }
  StatName tagExtractedStatName() const override { return counter_->tagExtractedStatName(); }
  bool used() const override { return counter_->used(); }
  bool hidden() const override { return counter_->hidden(); }
  SymbolTable& symbolTable() override { return counter_->symbolTable(); }
  const SymbolTable& constSymbolTable() const override { return counter_->constSymbolTable(); }

private:
  std::unique_ptr<Stats::Counter> counter_;
  absl::Mutex& mutex_;
  absl::CondVar& condvar_;
};

// A stats allocator which creates NotifyingCounters rather than regular CounterImpls.
class NotifyingAllocatorImpl : public Stats::AllocatorImpl {
public:
  using Stats::AllocatorImpl::AllocatorImpl;

  void waitForCounterFromStringEq(const std::string& name, uint64_t value) {
    absl::MutexLock l(&mutex_);
    ENVOY_LOG_MISC(trace, "waiting for {} to be {}", name, value);
    while (getCounterLockHeld(name) == nullptr || getCounterLockHeld(name)->value() != value) {
      condvar_.Wait(&mutex_);
    }
    ENVOY_LOG_MISC(trace, "done waiting for {} to be {}", name, value);
  }

  void waitForCounterFromStringGe(const std::string& name, uint64_t value) {
    absl::MutexLock l(&mutex_);
    ENVOY_LOG_MISC(trace, "waiting for {} to be {}", name, value);
    while (getCounterLockHeld(name) == nullptr || getCounterLockHeld(name)->value() < value) {
      condvar_.Wait(&mutex_);
    }
    ENVOY_LOG_MISC(trace, "done waiting for {} to be {}", name, value);
  }

  void waitForCounterExists(const std::string& name) {
    absl::MutexLock l(&mutex_);
    ENVOY_LOG_MISC(trace, "waiting for {} to exist", name);
    while (getCounterLockHeld(name) == nullptr) {
      condvar_.Wait(&mutex_);
    }
    ENVOY_LOG_MISC(trace, "done waiting for {} to exist", name);
  }

protected:
  Stats::Counter* makeCounterInternal(StatName name, StatName tag_extracted_name,
                                      const StatNameTagVector& stat_name_tags) override {
    Stats::Counter* counter = new NotifyingCounter(
        Stats::AllocatorImpl::makeCounterInternal(name, tag_extracted_name, stat_name_tags), mutex_,
        condvar_);
    {
      absl::MutexLock l(&mutex_);
      // Allow getting the counter directly from the allocator, since it's harder to
      // signal when the counter has been added to a given stats store.
      counters_.emplace(counter->name(), counter);
      if (counter->name() == "cluster_manager.cluster_removed") {
      }
      condvar_.Signal();
    }
    return counter;
  }

  virtual Stats::Counter* getCounterLockHeld(const std::string& name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    auto it = counters_.find(name);
    if (it != counters_.end()) {
      return it->second;
    }
    return nullptr;
  }

private:
  absl::flat_hash_map<std::string, Stats::Counter*> counters_;
  absl::Mutex mutex_;
  absl::CondVar condvar_;
};

/**
 * This is a variant of the isolated store that has locking across all operations so that it can
 * be used during the integration tests.
 */
class TestIsolatedStoreImpl : public StoreRoot {
public:
  // Stats::Store
  void forEachCounter(Stats::SizeFn f_size, StatFn<Counter> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachCounter(f_size, f_stat);
  }
  void forEachGauge(Stats::SizeFn f_size, StatFn<Gauge> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachGauge(f_size, f_stat);
  }
  void forEachTextReadout(Stats::SizeFn f_size, StatFn<TextReadout> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachTextReadout(f_size, f_stat);
  }
  void forEachHistogram(Stats::SizeFn f_size, StatFn<ParentHistogram> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachHistogram(f_size, f_stat);
  }
  void forEachScope(std::function<void(std::size_t)> f_size,
                    StatFn<const Scope> f_scope) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachScope(f_size, f_scope);
  }
  void forEachSinkedCounter(Stats::SizeFn f_size, StatFn<Counter> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachSinkedCounter(f_size, f_stat);
  }
  void forEachSinkedGauge(Stats::SizeFn f_size, StatFn<Gauge> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachSinkedGauge(f_size, f_stat);
  }
  void forEachSinkedTextReadout(Stats::SizeFn f_size, StatFn<TextReadout> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachSinkedTextReadout(f_size, f_stat);
  }
  void forEachSinkedHistogram(Stats::SizeFn f_size, StatFn<ParentHistogram> f_stat) const override {
    Thread::LockGuard lock(lock_);
    store_.forEachSinkedHistogram(f_size, f_stat);
  }
  void setSinkPredicates(std::unique_ptr<SinkPredicates>&& sink_predicates) override {
    UNREFERENCED_PARAMETER(sink_predicates);
  }
  OptRef<SinkPredicates> sinkPredicates() override { return OptRef<SinkPredicates>{}; }
  void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) override {
    Thread::LockGuard lock(lock_);
    store_.deliverHistogramToSinks(histogram, value);
  }
  NullGaugeImpl& nullGauge() override { return store_.nullGauge(); }
  NullCounterImpl& nullCounter() override { return store_.nullCounter(); }
  ScopeSharedPtr rootScope() override {
    Thread::LockGuard lock(lock_);
    if (lazy_default_scope_ == nullptr) {
      lazy_default_scope_ = std::make_shared<TestScopeWrapper>(lock_, store_.rootScope(), *this);
    }
    return lazy_default_scope_;
  }
  ConstScopeSharedPtr constRootScope() const override {
    return const_cast<TestIsolatedStoreImpl*>(this)->rootScope();
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
  std::vector<TextReadoutSharedPtr> textReadouts() const override {
    Thread::LockGuard lock(lock_);
    return store_.textReadouts();
  }

  bool iterate(const IterateFn<Counter>& fn) const override { return store_.iterate(fn); }
  bool iterate(const IterateFn<Gauge>& fn) const override { return store_.iterate(fn); }
  bool iterate(const IterateFn<Histogram>& fn) const override { return store_.iterate(fn); }
  bool iterate(const IterateFn<TextReadout>& fn) const override { return store_.iterate(fn); }

  void extractAndAppendTags(StatName, StatNamePool&, StatNameTagVector&) override{};
  void extractAndAppendTags(absl::string_view, StatNamePool&, StatNameTagVector&) override{};
  const Stats::TagVector& fixedTags() override { CONSTRUCT_ON_FIRST_USE(Stats::TagVector); }

  // Stats::StoreRoot
  void addSink(Sink&) override {}
  void setTagProducer(TagProducerPtr&&) override {}
  void setStatsMatcher(StatsMatcherPtr&&) override {}
  void setHistogramSettings(HistogramSettingsConstPtr&&) override {}
  void initializeThreading(Event::Dispatcher&, ThreadLocal::Instance&) override {}
  void shutdownThreading() override {}
  void mergeHistograms(PostMergeCb cb) override { merge_cb_ = cb; }

  void runMergeCallback() { merge_cb_(); }

private:
  mutable Thread::MutexBasicLockable lock_;
  IsolatedStoreImpl store_;
  PostMergeCb merge_cb_;
  ScopeSharedPtr lazy_default_scope_;
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
         std::function<void(IntegrationTestServer&)> on_server_ready_function,
         std::function<void()> on_server_init_function,
         absl::optional<uint64_t> deterministic_value, Event::TestTimeSystem& time_system,
         Api::Api& api, bool defer_listener_finalization = false,
         ProcessObjectOptRef process_object = absl::nullopt,
         Server::FieldValidationConfig validation_config = Server::FieldValidationConfig(),
         uint32_t concurrency = 1, std::chrono::seconds drain_time = std::chrono::seconds(1),
         Server::DrainStrategy drain_strategy = Server::DrainStrategy::Gradual,
         Buffer::WatermarkFactorySharedPtr watermark_factory = nullptr, bool use_real_stats = false,
         bool use_bootstrap_node_metadata = false,
         std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>&& config_proto = nullptr);
  // Note that the derived class is responsible for tearing down the server in its
  // destructor.
  ~IntegrationTestServer() override;

  void waitUntilListenersReady();

  void setDynamicContextParam(absl::string_view resource_type_url, absl::string_view key,
                              absl::string_view value);
  void unsetDynamicContextParam(absl::string_view resource_type_url, absl::string_view key);

  Server::DrainManagerImpl& drainManager() { return *drain_manager_; }
  void setOnWorkerListenerAddedCb(std::function<void()> on_worker_listener_added) {
    on_worker_listener_added_cb_ = std::move(on_worker_listener_added);
  }
  void setOnWorkerListenerRemovedCb(std::function<void()> on_worker_listener_removed) {
    on_worker_listener_removed_cb_ = std::move(on_worker_listener_removed);
  }
  void setOnServerReadyCb(std::function<void(IntegrationTestServer&)> on_server_ready) {
    on_server_ready_cb_ = std::move(on_server_ready);
  }
  void onWorkersStarted() override {}

  void start(const Network::Address::IpVersion version,
             std::function<void()> on_server_init_function,
             absl::optional<uint64_t> deterministic_value, bool defer_listener_finalization,
             ProcessObjectOptRef process_object, Server::FieldValidationConfig validation_config,
             uint32_t concurrency, std::chrono::seconds drain_time,
             Server::DrainStrategy drain_strategy,
             Buffer::WatermarkFactorySharedPtr watermark_factory, bool use_bootstrap_node_metadata);

  void waitForCounterEq(const std::string& name, uint64_t value,
                        std::chrono::milliseconds timeout = TestUtility::DefaultTimeout,
                        Event::Dispatcher* dispatcher = nullptr) override {
    ASSERT_TRUE(
        TestUtility::waitForCounterEq(statStore(), name, value, time_system_, timeout, dispatcher));
  }

  void waitForCounterGe(const std::string& name, uint64_t value,
                        std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) override {
    ASSERT_TRUE(TestUtility::waitForCounterGe(statStore(), name, value, time_system_, timeout));
  }

  void waitForGaugeEq(const std::string& name, uint64_t value,
                      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) override {
    ASSERT_TRUE(TestUtility::waitForGaugeEq(statStore(), name, value, time_system_, timeout));
  }

  void waitForGaugeGe(const std::string& name, uint64_t value,
                      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) override {
    ASSERT_TRUE(TestUtility::waitForGaugeGe(statStore(), name, value, time_system_, timeout));
  }

  void waitForCounterExists(const std::string& name) override {
    notifyingStatsAllocator().waitForCounterExists(name);
  }

  void waitForCounterNonexistent(const std::string& name,
                                 std::chrono::milliseconds timeout) override {
    Event::TestTimeSystem::RealTimeBound bound(timeout);
    while (TestUtility::findCounter(statStore(), name) != nullptr) {
      time_system_.advanceTimeWait(std::chrono::milliseconds(10));
      ASSERT_FALSE(!bound.withinBound())
          << "timed out waiting for counter " << name << " to not exist.";
    }
  }

  void waitForProactiveOverloadResourceUsageEq(
      const Server::OverloadProactiveResourceName resource_name, int64_t value,
      Event::Dispatcher& dispatcher,
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    ASSERT_TRUE(TestUtility::waitForProactiveOverloadResourceUsageEq(
        overloadState(), resource_name, value, time_system_, dispatcher, timeout));
  }

  // TODO(#17956): Add Gauge type to NotifyingAllocator and adopt it in this method.
  void waitForGaugeDestroyed(const std::string& name) override {
    ASSERT_TRUE(TestUtility::waitForGaugeDestroyed(statStore(), name, time_system_));
  }

  void waitUntilHistogramHasSamples(
      const std::string& name,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override {
    waitForNumHistogramSamplesGe(name, 1, timeout);
  }

  void waitForNumHistogramSamplesGe(
      const std::string& name, uint64_t sample_count,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override {
    ASSERT_TRUE(TestUtility::waitForNumHistogramSamplesGe(
        statStore(), name, sample_count, time_system_, server().dispatcher(), timeout));
  }

  Stats::CounterSharedPtr counter(const std::string& name) override {
    // When using the thread local store, only counters() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findCounter(statStore(), name);
  }

  Stats::GaugeSharedPtr gauge(const std::string& name) override {
    // When using the thread local store, only gauges() is thread safe. This also allows us
    // to test if a counter exists at all versus just defaulting to zero.
    return TestUtility::findGauge(statStore(), name);
  }

  Stats::ParentHistogramSharedPtr histogram(const std::string& name) {
    return TestUtility::findHistogram(statStore(), name);
  }

  std::vector<Stats::CounterSharedPtr> counters() override { return statStore().counters(); }

  std::vector<Stats::GaugeSharedPtr> gauges() override { return statStore().gauges(); }

  std::vector<Stats::ParentHistogramSharedPtr> histograms() override {
    return statStore().histograms();
  }

  // ListenerHooks
  void onWorkerListenerAdded() override;
  void onWorkerListenerRemoved() override;

  // Server::ComponentFactory
  Server::DrainManagerPtr createDrainManager(Server::Instance& server) override {
    drain_manager_ = new Server::DrainManagerImpl(
        server, envoy::config::listener::v3::Listener::MODIFY_ONLY, server.dispatcher());
    return Server::DrainManagerPtr{drain_manager_};
  }
  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override {
    return Server::InstanceUtil::createRuntime(server, config);
  }

  // Should not be called until createAndRunEnvoyServer() is called.
  virtual Server::Instance& server() PURE;
  virtual Stats::Store& statStore() PURE;
  virtual Server::ThreadLocalOverloadState& overloadState() PURE;
  virtual Network::Address::InstanceConstSharedPtr adminAddress() PURE;
  virtual Stats::NotifyingAllocatorImpl& notifyingStatsAllocator() PURE;
  void useAdminInterfaceToQuit(bool use) { use_admin_interface_to_quit_ = use; }
  bool useAdminInterfaceToQuit() { return use_admin_interface_to_quit_; }

protected:
  IntegrationTestServer(Event::TestTimeSystem& time_system, Api::Api& api,
                        const std::string& config_path,
                        std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>&& config_proto)
      : time_system_(time_system), api_(api), config_path_(config_path),
        config_proto_(std::move(config_proto)) {}

  // Create the running envoy server. This function will call serverReady() when the virtual
  // functions server(), statStore(), and adminAddress() may be called, but before the server
  // has been started.
  // The subclass is also responsible for tearing down this server in its destructor.
  virtual void createAndRunEnvoyServer(OptionsImplBase& options, Event::TimeSystem& time_system,
                                       Network::Address::InstanceConstSharedPtr local_address,
                                       ListenerHooks& hooks, Thread::BasicLockable& access_log_lock,
                                       Server::ComponentFactory& component_factory,
                                       Random::RandomGeneratorPtr&& random_generator,
                                       ProcessObjectOptRef process_object,
                                       Buffer::WatermarkFactorySharedPtr watermark_factory) PURE;

  // Will be called by subclass on server thread when the server is ready to be accessed. The
  // server may not have been run yet, but all server access methods (server(), statStore(),
  // adminAddress()) will be available.
  void serverReady();

private:
  /**
   * Runs the real server on a thread.
   */
  void threadRoutine(const Network::Address::IpVersion version,
                     absl::optional<uint64_t> deterministic_value,
                     ProcessObjectOptRef process_object,
                     Server::FieldValidationConfig validation_config, uint32_t concurrency,
                     std::chrono::seconds drain_time, Server::DrainStrategy drain_strategy,
                     Buffer::WatermarkFactorySharedPtr watermark_factory,
                     bool use_bootstrap_node_metadata);

  Event::TestTimeSystem& time_system_;
  Api::Api& api_;
  const std::string config_path_;
  Thread::ThreadPtr thread_;
  Thread::CondVar listeners_cv_;
  Thread::MutexBasicLockable listeners_mutex_;
  uint64_t pending_listeners_;
  ConditionalInitializer server_set_;
  Server::DrainManagerImpl* drain_manager_{};
  std::function<void()> on_worker_listener_added_cb_;
  std::function<void()> on_worker_listener_removed_cb_;
  TcpDumpPtr tcp_dump_;
  std::function<void(IntegrationTestServer&)> on_server_ready_cb_;
  bool use_admin_interface_to_quit_{};
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> config_proto_;
};

// Default implementation of IntegrationTestServer
class IntegrationTestServerImpl : public IntegrationTestServer {
public:
  IntegrationTestServerImpl(
      Event::TestTimeSystem& time_system, Api::Api& api, const std::string& config_path,
      bool real_stats = false,
      std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>&& config_proto = nullptr);

  ~IntegrationTestServerImpl() override;

  Server::Instance& server() override {
    RELEASE_ASSERT(server_ != nullptr, "");
    return *server_;
  }
  Stats::Store& statStore() override {
    RELEASE_ASSERT(stat_store_ != nullptr, "");
    return *stat_store_;
  }
  Server::ThreadLocalOverloadState& overloadState() override {
    RELEASE_ASSERT(server_ != nullptr, "");
    return server_->overloadManager().getThreadLocalOverloadState();
  }

  Network::Address::InstanceConstSharedPtr adminAddress() override { return admin_address_; }

  Stats::NotifyingAllocatorImpl& notifyingStatsAllocator() override {
    auto* ret = dynamic_cast<Stats::NotifyingAllocatorImpl*>(stats_allocator_.get());
    RELEASE_ASSERT(ret != nullptr,
                   "notifyingStatsAllocator() is not created when real_stats is true");
    return *ret;
  }

private:
  void createAndRunEnvoyServer(OptionsImplBase& options, Event::TimeSystem& time_system,
                               Network::Address::InstanceConstSharedPtr local_address,
                               ListenerHooks& hooks, Thread::BasicLockable& access_log_lock,
                               Server::ComponentFactory& component_factory,
                               Random::RandomGeneratorPtr&& random_generator,
                               ProcessObjectOptRef process_object,
                               Buffer::WatermarkFactorySharedPtr watermark_factory) override;

  // Owned by this class. An owning pointer is not used because the actual allocation is done
  // on a stack in a non-main thread.
  Server::Instance* server_{};
  Stats::Store* stat_store_{};
  Network::Address::InstanceConstSharedPtr admin_address_;
  absl::Notification server_gone_;
  Stats::SymbolTableImpl symbol_table_;
  std::unique_ptr<Stats::AllocatorImpl> stats_allocator_;
};

} // namespace Envoy
