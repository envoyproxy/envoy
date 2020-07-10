#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/instance.h"
#include "envoy/server/process_context.h"
#include "envoy/server/tracer_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"
#include "envoy/tracing/http_tracer.h"

#include "common/access_log/access_log_manager_impl.h"
#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/logger_delegates.h"
#include "common/grpc/async_client_manager_impl.h"
#include "common/grpc/context_impl.h"
#include "common/http/context_impl.h"
#include "common/init/manager_impl.h"
#include "common/memory/heap_shrinker.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/secret/secret_manager_impl.h"
#include "common/upstream/health_discovery_service.h"

#include "server/admin/admin.h"
#include "server/configuration_impl.h"
#include "server/listener_hooks.h"
#include "server/listener_manager_impl.h"
#include "server/overload_manager_impl.h"
#include "server/worker_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

/**
 * All server wide stats. @see stats_macros.h
 */
#define ALL_SERVER_STATS(COUNTER, GAUGE, HISTOGRAM)                                                \
  COUNTER(debug_assertion_failures)                                                                \
  COUNTER(envoy_bug_failures)                                                                      \
  COUNTER(dynamic_unknown_fields)                                                                  \
  COUNTER(static_unknown_fields)                                                                   \
  GAUGE(concurrency, NeverImport)                                                                  \
  GAUGE(days_until_first_cert_expiring, Accumulate)                                                \
  GAUGE(hot_restart_epoch, NeverImport)                                                            \
  /* hot_restart_generation is an Accumulate gauge; we omit it here for testing dynamics. */       \
  GAUGE(live, NeverImport)                                                                         \
  GAUGE(memory_allocated, Accumulate)                                                              \
  GAUGE(memory_heap_size, Accumulate)                                                              \
  GAUGE(memory_physical_size, Accumulate)                                                          \
  GAUGE(parent_connections, Accumulate)                                                            \
  GAUGE(state, NeverImport)                                                                        \
  GAUGE(stats_recent_lookups, NeverImport)                                                         \
  GAUGE(total_connections, Accumulate)                                                             \
  GAUGE(uptime, Accumulate)                                                                        \
  GAUGE(version, NeverImport)                                                                      \
  HISTOGRAM(initialization_time_ms, Milliseconds)

struct ServerStats {
  ALL_SERVER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Interface for creating service components during boot.
 */
class ComponentFactory {
public:
  virtual ~ComponentFactory() = default;

  /**
   * @return DrainManagerPtr a new drain manager for the server.
   */
  virtual DrainManagerPtr createDrainManager(Instance& server) PURE;

  /**
   * @return Runtime::LoaderPtr the runtime implementation for the server.
   */
  virtual Runtime::LoaderPtr createRuntime(Instance& server, Configuration::Initial& config) PURE;
};

/**
 * Helpers used during server creation.
 */
class InstanceUtil : Logger::Loggable<Logger::Id::main> {
public:
  /**
   * Default implementation of runtime loader creation used in the real server and in most
   * integration tests where a mock runtime is not needed.
   */
  static Runtime::LoaderPtr createRuntime(Instance& server, Server::Configuration::Initial& config);

  /**
   * Helper for flushing counters, gauges and histograms to sinks. This takes care of calling
   * flush() on each sink.
   * @param sinks supplies the list of sinks.
   * @param store provides the store being flushed.
   */
  static void flushMetricsToSinks(const std::list<Stats::SinkPtr>& sinks, Stats::Store& store);

  /**
   * Load a bootstrap config and perform validation.
   * @param bootstrap supplies the bootstrap to fill.
   * @param options supplies the server options.
   * @param validation_visitor message validation visitor instance.
   * @param api reference to the Api object
   */
  static void loadBootstrapConfig(envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                  const Options& options,
                                  ProtobufMessage::ValidationVisitor& validation_visitor,
                                  Api::Api& api);
};

/**
 * This is a helper used by InstanceImpl::run() on the stack. It's broken out to make testing
 * easier.
 */
class RunHelper : Logger::Loggable<Logger::Id::main> {
public:
  RunHelper(Instance& instance, const Options& options, Event::Dispatcher& dispatcher,
            Upstream::ClusterManager& cm, AccessLog::AccessLogManager& access_log_manager,
            Init::Manager& init_manager, OverloadManager& overload_manager,
            std::function<void()> workers_start_cb);

private:
  Init::WatcherImpl init_watcher_;
  Event::SignalEventPtr sigterm_;
  Event::SignalEventPtr sigint_;
  Event::SignalEventPtr sig_usr_1_;
  Event::SignalEventPtr sig_hup_;
};

// ServerFactoryContextImpl implements both ServerFactoryContext and
// TransportSocketFactoryContext for convenience as these two contexts
// share common member functions and member variables.
class ServerFactoryContextImpl : public Configuration::ServerFactoryContext,
                                 public Configuration::TransportSocketFactoryContext {
public:
  explicit ServerFactoryContextImpl(Instance& server)
      : server_(server), server_scope_(server_.stats().createScope("")) {}

  // Configuration::ServerFactoryContext
  Upstream::ClusterManager& clusterManager() override { return server_.clusterManager(); }
  Event::Dispatcher& dispatcher() override { return server_.dispatcher(); }
  const LocalInfo::LocalInfo& localInfo() const override { return server_.localInfo(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return server_.messageValidationContext();
  }
  Envoy::Runtime::RandomGenerator& random() override { return server_.random(); }
  Envoy::Runtime::Loader& runtime() override { return server_.runtime(); }
  Stats::Scope& scope() override { return *server_scope_; }
  Singleton::Manager& singletonManager() override { return server_.singletonManager(); }
  ThreadLocal::Instance& threadLocal() override { return server_.threadLocal(); }
  Admin& admin() override { return server_.admin(); }
  TimeSource& timeSource() override { return api().timeSource(); }
  Api::Api& api() override { return server_.api(); }
  Grpc::Context& grpcContext() override { return server_.grpcContext(); }
  Envoy::Server::DrainManager& drainManager() override { return server_.drainManager(); }
  ServerLifecycleNotifier& lifecycleNotifier() override { return server_.lifecycleNotifier(); }

  // Configuration::TransportSocketFactoryContext
  Ssl::ContextManager& sslContextManager() override { return server_.sslContextManager(); }
  Secret::SecretManager& secretManager() override { return server_.secretManager(); }
  Stats::Store& stats() override { return server_.stats(); }
  Init::Manager& initManager() override { return server_.initManager(); }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    // Server has two message validation visitors, one for static and
    // other for dynamic configuration. Choose the dynamic validation
    // visitor if server's init manager indicates that the server is
    // in the Initialized state, as this state is engaged right after
    // the static configuration (e.g., bootstrap) has been completed.
    return initManager().state() == Init::Manager::State::Initialized
               ? server_.messageValidationContext().dynamicValidationVisitor()
               : server_.messageValidationContext().staticValidationVisitor();
  }

private:
  Instance& server_;
  Stats::ScopePtr server_scope_;
};

/**
 * This is the actual full standalone server which stitches together various common components.
 */
class InstanceImpl final : Logger::Loggable<Logger::Id::main>,
                           public Instance,
                           public ServerLifecycleNotifier {
public:
  /**
   * @throw EnvoyException if initialization fails.
   */
  InstanceImpl(Init::Manager& init_manager, const Options& options, Event::TimeSystem& time_system,
               Network::Address::InstanceConstSharedPtr local_address, ListenerHooks& hooks,
               HotRestart& restarter, Stats::StoreRoot& store,
               Thread::BasicLockable& access_log_lock, ComponentFactory& component_factory,
               Runtime::RandomGeneratorPtr&& random_generator, ThreadLocal::Instance& tls,
               Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system,
               std::unique_ptr<ProcessContext> process_context);

  ~InstanceImpl() override;

  void run();

  // Server::Instance
  Admin& admin() override { return *admin_; }
  Api::Api& api() override { return *api_; }
  Upstream::ClusterManager& clusterManager() override;
  Ssl::ContextManager& sslContextManager() override { return *ssl_context_manager_; }
  Event::Dispatcher& dispatcher() override { return *dispatcher_; }
  Network::DnsResolverSharedPtr dnsResolver() override { return dns_resolver_; }
  void drainListeners() override;
  DrainManager& drainManager() override { return *drain_manager_; }
  AccessLog::AccessLogManager& accessLogManager() override { return access_log_manager_; }
  void failHealthcheck(bool fail) override;
  HotRestart& hotRestart() override { return restarter_; }
  Init::Manager& initManager() override { return init_manager_; }
  ServerLifecycleNotifier& lifecycleNotifier() override { return *this; }
  ListenerManager& listenerManager() override { return *listener_manager_; }
  Secret::SecretManager& secretManager() override { return *secret_manager_; }
  Envoy::MutexTracer* mutexTracer() override { return mutex_tracer_; }
  OverloadManager& overloadManager() override { return *overload_manager_; }
  Runtime::RandomGenerator& random() override { return *random_generator_; }
  Runtime::Loader& runtime() override;
  void shutdown() override;
  bool isShutdown() final { return shutdown_; }
  void shutdownAdmin() override;
  Singleton::Manager& singletonManager() override { return *singleton_manager_; }
  bool healthCheckFailed() override;
  const Options& options() override { return options_; }
  time_t startTimeCurrentEpoch() override { return start_time_; }
  time_t startTimeFirstEpoch() override { return original_start_time_; }
  Stats::Store& stats() override { return stats_store_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }
  ProcessContextOptRef processContext() override { return *process_context_; }
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  const LocalInfo::LocalInfo& localInfo() const override { return *local_info_; }
  TimeSource& timeSource() override { return time_source_; }
  void flushStats() override;

  Configuration::ServerFactoryContext& serverFactoryContext() override { return server_contexts_; }

  Configuration::TransportSocketFactoryContext& transportSocketFactoryContext() override {
    return server_contexts_;
  }

  std::chrono::milliseconds statsFlushInterval() const override {
    return config_.statsFlushInterval();
  }

  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return validation_context_;
  }

  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) override {
    http_context_.setDefaultTracingConfig(tracing_config);
  }

  // ServerLifecycleNotifier
  ServerLifecycleNotifier::HandlePtr registerCallback(Stage stage, StageCallback callback) override;
  ServerLifecycleNotifier::HandlePtr
  registerCallback(Stage stage, StageCallbackWithCompletion callback) override;

private:
  ProtobufTypes::MessagePtr dumpBootstrapConfig();
  void flushStatsInternal();
  void updateServerStats();
  void initialize(const Options& options, Network::Address::InstanceConstSharedPtr local_address,
                  ComponentFactory& component_factory, ListenerHooks& hooks);
  void loadServerFlags(const absl::optional<std::string>& flags_path);
  void startWorkers();
  void terminate();
  void notifyCallbacksForStage(
      Stage stage, Event::PostCb completion_cb = [] {});
  void onRuntimeReady();
  void onClusterManagerPrimaryInitializationComplete();

  using LifecycleNotifierCallbacks = std::list<StageCallback>;
  using LifecycleNotifierCompletionCallbacks = std::list<StageCallbackWithCompletion>;

  // init_manager_ must come before any member that participates in initialization, and destructed
  // only after referencing members are gone, since initialization continuation can potentially
  // occur at any point during member lifetime. This init manager is populated with LdsApi targets.
  Init::Manager& init_manager_;
  // secret_manager_ must come before listener_manager_, config_ and dispatcher_, and destructed
  // only after these members can no longer reference it, since:
  // - There may be active filter chains referencing it in listener_manager_.
  // - There may be active clusters referencing it in config_.cluster_manager_.
  // - There may be active connections referencing it.
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  bool workers_started_;
  std::atomic<bool> live_;
  bool shutdown_;
  const Options& options_;
  ProtobufMessage::ProdValidationContextImpl validation_context_;
  TimeSource& time_source_;
  // Delete local_info_ as late as possible as some members below may reference it during their
  // destruction.
  LocalInfo::LocalInfoPtr local_info_;
  HotRestart& restarter_;
  const time_t start_time_;
  time_t original_start_time_;
  Stats::StoreRoot& stats_store_;
  std::unique_ptr<ServerStats> server_stats_;
  Assert::ActionRegistrationPtr assert_action_registration_;
  Assert::ActionRegistrationPtr envoy_bug_action_registration_;
  ThreadLocal::Instance& thread_local_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<AdminImpl> admin_;
  Singleton::ManagerPtr singleton_manager_;
  Network::ConnectionHandlerPtr handler_;
  Runtime::RandomGeneratorPtr random_generator_;
  std::unique_ptr<Runtime::ScopedLoaderSingleton> runtime_singleton_;
  std::unique_ptr<Ssl::ContextManager> ssl_context_manager_;
  ProdListenerComponentFactory listener_component_factory_;
  ProdWorkerFactory worker_factory_;
  std::unique_ptr<ListenerManager> listener_manager_;
  absl::node_hash_map<Stage, LifecycleNotifierCallbacks> stage_callbacks_;
  absl::node_hash_map<Stage, LifecycleNotifierCompletionCallbacks> stage_completable_callbacks_;
  Configuration::MainImpl config_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Event::TimerPtr stat_flush_timer_;
  DrainManagerPtr drain_manager_;
  AccessLog::AccessLogManagerImpl access_log_manager_;
  std::unique_ptr<Upstream::ClusterManagerFactory> cluster_manager_factory_;
  std::unique_ptr<Server::GuardDog> guard_dog_;
  bool terminated_;
  std::unique_ptr<Logger::FileSinkDelegate> file_logger_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  SystemTime bootstrap_config_update_time_;
  Grpc::AsyncClientManagerPtr async_client_manager_;
  Upstream::ProdClusterInfoFactory info_factory_;
  Upstream::HdsDelegatePtr hds_delegate_;
  std::unique_ptr<OverloadManagerImpl> overload_manager_;
  std::vector<BootstrapExtensionPtr> bootstrap_extensions_;
  Envoy::MutexTracer* mutex_tracer_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  std::unique_ptr<ProcessContext> process_context_;
  std::unique_ptr<Memory::HeapShrinker> heap_shrinker_;
  const std::thread::id main_thread_id_;
  // initialization_time is a histogram for tracking the initialization time across hot restarts
  // whenever we have support for histogram merge across hot restarts.
  Stats::TimespanPtr initialization_timer_;

  ServerFactoryContextImpl server_contexts_;

  template <class T>
  class LifecycleCallbackHandle : public ServerLifecycleNotifier::Handle, RaiiListElement<T> {
  public:
    LifecycleCallbackHandle(std::list<T>& callbacks, T& callback)
        : RaiiListElement<T>(callbacks, callback) {}
  };
};

// Local implementation of Stats::MetricSnapshot used to flush metrics to sinks. We could
// potentially have a single class instance held in a static and have a clear() method to avoid some
// vector constructions and reservations, but I'm not sure it's worth the extra complexity until it
// shows up in perf traces.
// TODO(mattklein123): One thing we probably want to do is switch from returning vectors of metrics
//                     to a lambda based callback iteration API. This would require less vector
//                     copying and probably be a cleaner API in general.
class MetricSnapshotImpl : public Stats::MetricSnapshot {
public:
  explicit MetricSnapshotImpl(Stats::Store& store);

  // Stats::MetricSnapshot
  const std::vector<CounterSnapshot>& counters() override { return counters_; }
  const std::vector<std::reference_wrapper<const Stats::Gauge>>& gauges() override {
    return gauges_;
  };
  const std::vector<std::reference_wrapper<const Stats::ParentHistogram>>& histograms() override {
    return histograms_;
  }
  const std::vector<std::reference_wrapper<const Stats::TextReadout>>& textReadouts() override {
    return text_readouts_;
  }

private:
  std::vector<Stats::CounterSharedPtr> snapped_counters_;
  std::vector<CounterSnapshot> counters_;
  std::vector<Stats::GaugeSharedPtr> snapped_gauges_;
  std::vector<std::reference_wrapper<const Stats::Gauge>> gauges_;
  std::vector<Stats::ParentHistogramSharedPtr> snapped_histograms_;
  std::vector<std::reference_wrapper<const Stats::ParentHistogram>> histograms_;
  std::vector<Stats::TextReadoutSharedPtr> snapped_text_readouts_;
  std::vector<std::reference_wrapper<const Stats::TextReadout>> text_readouts_;
};

} // namespace Server
} // namespace Envoy
