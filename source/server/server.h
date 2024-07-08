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
#include "envoy/server/configuration.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/instance.h"
#include "envoy/server/process_context.h"
#include "envoy/server/tracer_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"
#include "envoy/tracing/tracer.h"

#include "source/common/access_log/access_log_manager_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger_delegates.h"
#include "source/common/common/perf_tracing.h"
#include "source/common/grpc/async_client_manager_impl.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/init/manager_impl.h"
#include "source/common/memory/stats.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/router/context_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/health_discovery_service.h"

#ifdef ENVOY_ADMIN_FUNCTIONALITY
#include "source/server/admin/admin.h"
#endif
#include "source/server/configuration_impl.h"
#include "source/server/listener_hooks.h"
#include "source/server/worker_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Server {
namespace CompilationSettings {
/**
 * All server compilation settings stats. @see stats_macros.h
 */
#define ALL_SERVER_COMPILATION_SETTINGS_STATS(COUNTER, GAUGE, HISTOGRAM)                           \
  GAUGE(fips_mode, NeverImport)

struct ServerCompilationSettingsStats {
  ALL_SERVER_COMPILATION_SETTINGS_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                                        GENERATE_HISTOGRAM_STRUCT)
};
} // namespace CompilationSettings

/**
 * All server wide stats. @see stats_macros.h
 */
#define ALL_SERVER_STATS(COUNTER, GAUGE, HISTOGRAM)                                                \
  COUNTER(debug_assertion_failures)                                                                \
  COUNTER(envoy_bug_failures)                                                                      \
  COUNTER(dynamic_unknown_fields)                                                                  \
  COUNTER(static_unknown_fields)                                                                   \
  COUNTER(wip_protos)                                                                              \
  COUNTER(dropped_stat_flushes)                                                                    \
  GAUGE(concurrency, NeverImport)                                                                  \
  GAUGE(days_until_first_cert_expiring, NeverImport)                                               \
  GAUGE(seconds_until_first_ocsp_response_expiring, NeverImport)                                   \
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
  static void flushMetricsToSinks(const std::list<Stats::SinkPtr>& sinks, Stats::Store& store,
                                  Upstream::ClusterManager& cm, TimeSource& time_source);

  /**
   * Load a bootstrap config and perform validation.
   * @param bootstrap supplies the bootstrap to fill.
   * @param options supplies the server options.
   * @param validation_visitor message validation visitor instance.
   * @param api reference to the Api object
   */
  static absl::Status loadBootstrapConfig(envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                          const Options& options,
                                          ProtobufMessage::ValidationVisitor& validation_visitor,
                                          Api::Api& api);
};

/**
 * This is a helper used by InstanceBase::run() on the stack. It's broken out to make testing
 * easier.
 */
class RunHelper : Logger::Loggable<Logger::Id::main> {
public:
  RunHelper(Instance& instance, const Options& options, Event::Dispatcher& dispatcher,
            Upstream::ClusterManager& cm, AccessLog::AccessLogManager& access_log_manager,
            Init::Manager& init_manager, OverloadManager& overload_manager,
            OverloadManager& null_overload_manager, std::function<void()> workers_start_cb);

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
  Event::Dispatcher& mainThreadDispatcher() override { return server_.dispatcher(); }
  const Server::Options& options() override { return server_.options(); }
  const LocalInfo::LocalInfo& localInfo() const override { return server_.localInfo(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return server_.messageValidationContext();
  }
  Envoy::Runtime::Loader& runtime() override { return server_.runtime(); }
  Stats::Scope& scope() override { return *server_scope_; }
  Stats::Scope& serverScope() override { return *server_scope_; }
  Singleton::Manager& singletonManager() override { return server_.singletonManager(); }
  ThreadLocal::Instance& threadLocal() override { return server_.threadLocal(); }
  OptRef<Admin> admin() override { return server_.admin(); }
  TimeSource& timeSource() override { return api().timeSource(); }
  AccessLog::AccessLogManager& accessLogManager() override { return server_.accessLogManager(); }
  Api::Api& api() override { return server_.api(); }
  Http::Context& httpContext() override { return server_.httpContext(); }
  Grpc::Context& grpcContext() override { return server_.grpcContext(); }
  Router::Context& routerContext() override { return server_.routerContext(); }
  ProcessContextOptRef processContext() override { return server_.processContext(); }
  Envoy::Server::DrainManager& drainManager() override { return server_.drainManager(); }
  ServerLifecycleNotifier& lifecycleNotifier() override { return server_.lifecycleNotifier(); }
  Regex::Engine& regexEngine() override { return server_.regexEngine(); }
  Configuration::StatsConfig& statsConfig() override { return server_.statsConfig(); }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return server_.bootstrap(); }
  OverloadManager& overloadManager() override { return server_.overloadManager(); }
  OverloadManager& nullOverloadManager() override { return server_.nullOverloadManager(); }
  bool healthCheckFailed() const override { return server_.healthCheckFailed(); }

  // Configuration::TransportSocketFactoryContext
  ServerFactoryContext& serverFactoryContext() override { return *this; }
  Ssl::ContextManager& sslContextManager() override { return server_.sslContextManager(); }
  Secret::SecretManager& secretManager() override { return server_.secretManager(); }
  Stats::Scope& statsScope() override { return *server_scope_; }
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
  Stats::ScopeSharedPtr server_scope_;
};

/**
 * This is the base class for the standalone server which stitches together various common
 * components. Some components are optional (so PURE) and can be created or not by subclasses.
 */
class InstanceBase : Logger::Loggable<Logger::Id::main>,
                     public Instance,
                     public ServerLifecycleNotifier {
public:
  /**
   * @throw EnvoyException if initialization fails.
   */
  InstanceBase(Init::Manager& init_manager, const Options& options, Event::TimeSystem& time_system,
               ListenerHooks& hooks, HotRestart& restarter, Stats::StoreRoot& store,
               Thread::BasicLockable& access_log_lock,
               Random::RandomGeneratorPtr&& random_generator, ThreadLocal::Instance& tls,
               Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system,
               std::unique_ptr<ProcessContext> process_context,
               Buffer::WatermarkFactorySharedPtr watermark_factory = nullptr);

  // initialize the server. This must be called before run().
  void initialize(Network::Address::InstanceConstSharedPtr local_address,
                  ComponentFactory& component_factory);
  ~InstanceBase() override;

  virtual void maybeCreateHeapShrinker() PURE;
  virtual std::unique_ptr<OverloadManager> createOverloadManager() PURE;
  virtual std::unique_ptr<OverloadManager> createNullOverloadManager() PURE;
  virtual std::unique_ptr<Server::GuardDog> maybeCreateGuardDog(absl::string_view name) PURE;

  void run() override;

  // Server::Instance
  OptRef<Admin> admin() override { return makeOptRefFromPtr(admin_.get()); }
  Api::Api& api() override { return *api_; }
  Upstream::ClusterManager& clusterManager() override;
  const Upstream::ClusterManager& clusterManager() const override;
  Ssl::ContextManager& sslContextManager() override { return *ssl_context_manager_; }
  Event::Dispatcher& dispatcher() override { return *dispatcher_; }
  Network::DnsResolverSharedPtr dnsResolver() override { return dns_resolver_; }
  void drainListeners(OptRef<const Network::ExtraShutdownListenerOptions> options) override;
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
  OverloadManager& nullOverloadManager() override { return *null_overload_manager_; }
  Runtime::Loader& runtime() override;
  void shutdown() override;
  bool isShutdown() final { return shutdown_; }
  void shutdownAdmin() override;
  Singleton::Manager& singletonManager() override { return singleton_manager_; }
  bool healthCheckFailed() override;
  const Options& options() override { return options_; }
  time_t startTimeCurrentEpoch() override { return start_time_; }
  time_t startTimeFirstEpoch() override { return original_start_time_; }
  Stats::Store& stats() override { return stats_store_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }
  Router::Context& routerContext() override { return router_context_; }
  ProcessContextOptRef processContext() override;
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  LocalInfo::LocalInfo& localInfo() const override { return *local_info_; }
  TimeSource& timeSource() override { return time_source_; }
  void flushStats() override;
  Configuration::StatsConfig& statsConfig() override { return config_.statsConfig(); }
  Regex::Engine& regexEngine() override { return *regex_engine_; }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return bootstrap_; }
  Configuration::ServerFactoryContext& serverFactoryContext() override { return server_contexts_; }
  Configuration::TransportSocketFactoryContext& transportSocketFactoryContext() override {
    return server_contexts_;
  }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return validation_context_;
  }
  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) override {
    http_context_.setDefaultTracingConfig(tracing_config);
  }
  bool enableReusePortDefault() override;

  Quic::QuicStatNames& quicStatNames() { return quic_stat_names_; }

  void setSinkPredicates(std::unique_ptr<Envoy::Stats::SinkPredicates>&& sink_predicates) override {
    stats_store_.setSinkPredicates(std::move(sink_predicates));
  }

  // ServerLifecycleNotifier
  ServerLifecycleNotifier::HandlePtr registerCallback(Stage stage, StageCallback callback) override;
  ServerLifecycleNotifier::HandlePtr
  registerCallback(Stage stage, StageCallbackWithCompletion callback) override;

protected:
  const Configuration::MainImpl& config() { return config_; }

private:
  Network::DnsResolverSharedPtr getOrCreateDnsResolver();

  ProtobufTypes::MessagePtr dumpBootstrapConfig();
  void flushStatsInternal();
  void updateServerStats();
  // This does most of the work of initialization, but can throw or return errors caught
  // by initialize().
  absl::Status initializeOrThrow(Network::Address::InstanceConstSharedPtr local_address,
                                 ComponentFactory& component_factory);
  void loadServerFlags(const absl::optional<std::string>& flags_path);
  void startWorkers();
  void terminate();
  void notifyCallbacksForStage(
      Stage stage, std::function<void()> completion_cb = [] {});
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
  bool workers_started_{false};
  std::atomic<bool> live_;
  bool shutdown_{false};
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
  std::unique_ptr<CompilationSettings::ServerCompilationSettingsStats>
      server_compilation_settings_stats_;
  Assert::ActionRegistrationPtr assert_action_registration_;
  Assert::ActionRegistrationPtr envoy_bug_action_registration_;
  ThreadLocal::Instance& thread_local_;
  Random::RandomGeneratorPtr random_generator_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  Api::ApiPtr api_;
  // ssl_context_manager_ must come before dispatcher_, since ClusterInfo
  // references SslSocketFactory and is deleted on the main thread via the dispatcher.
  std::unique_ptr<Ssl::ContextManager> ssl_context_manager_;
  Event::DispatcherPtr dispatcher_;
  AccessLog::AccessLogManagerImpl access_log_manager_;
  std::shared_ptr<Admin> admin_;
  Singleton::ManagerImpl singleton_manager_;
  Network::ConnectionHandlerPtr handler_;
  std::unique_ptr<Runtime::Loader> runtime_;
  ProdWorkerFactory worker_factory_;
  std::unique_ptr<ListenerManager> listener_manager_;
  absl::node_hash_map<Stage, LifecycleNotifierCallbacks> stage_callbacks_;
  absl::node_hash_map<Stage, LifecycleNotifierCompletionCallbacks> stage_completable_callbacks_;
  Configuration::MainImpl config_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Event::TimerPtr stat_flush_timer_;
  DrainManagerPtr drain_manager_;
  std::unique_ptr<Upstream::ClusterManagerFactory> cluster_manager_factory_;
  std::unique_ptr<Server::GuardDog> main_thread_guard_dog_;
  std::unique_ptr<Server::GuardDog> worker_guard_dog_;
  bool terminated_{false};
  std::unique_ptr<Logger::FileSinkDelegate> file_logger_;
  ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  SystemTime bootstrap_config_update_time_;
  Grpc::AsyncClientManagerPtr async_client_manager_;
  Upstream::ProdClusterInfoFactory info_factory_;
  Upstream::HdsDelegatePtr hds_delegate_;
  std::unique_ptr<OverloadManager> overload_manager_;
  std::unique_ptr<OverloadManager> null_overload_manager_;
  std::vector<BootstrapExtensionPtr> bootstrap_extensions_;
  Envoy::MutexTracer* mutex_tracer_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  Router::ContextImpl router_context_;
  std::unique_ptr<ProcessContext> process_context_;
  // initialization_time is a histogram for tracking the initialization time across hot restarts
  // whenever we have support for histogram merge across hot restarts.
  Stats::TimespanPtr initialization_timer_;
  ListenerHooks& hooks_;
  Quic::QuicStatNames quic_stat_names_;
  ServerFactoryContextImpl server_contexts_;
  bool enable_reuse_port_default_{false};
  Regex::EnginePtr regex_engine_;
  bool stats_flush_in_progress_ : 1;
  std::unique_ptr<Memory::AllocatorManager> memory_allocator_manager_;

  template <class T>
  class LifecycleCallbackHandle : public ServerLifecycleNotifier::Handle, RaiiListElement<T> {
  public:
    LifecycleCallbackHandle(std::list<T>& callbacks, T& callback)
        : RaiiListElement<T>(callbacks, callback) {}
  };

#ifdef ENVOY_PERFETTO
  std::unique_ptr<perfetto::TracingSession> tracing_session_{};
  os_fd_t tracing_fd_{INVALID_HANDLE};
#endif
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
  explicit MetricSnapshotImpl(Stats::Store& store, Upstream::ClusterManager& cluster_manager,
                              TimeSource& time_source);

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
  const std::vector<Stats::PrimitiveCounterSnapshot>& hostCounters() override {
    return host_counters_;
  }
  const std::vector<Stats::PrimitiveGaugeSnapshot>& hostGauges() override { return host_gauges_; }
  SystemTime snapshotTime() const override { return snapshot_time_; }

private:
  std::vector<Stats::CounterSharedPtr> snapped_counters_;
  std::vector<CounterSnapshot> counters_;
  std::vector<Stats::GaugeSharedPtr> snapped_gauges_;
  std::vector<std::reference_wrapper<const Stats::Gauge>> gauges_;
  std::vector<Stats::ParentHistogramSharedPtr> snapped_histograms_;
  std::vector<std::reference_wrapper<const Stats::ParentHistogram>> histograms_;
  std::vector<Stats::TextReadoutSharedPtr> snapped_text_readouts_;
  std::vector<std::reference_wrapper<const Stats::TextReadout>> text_readouts_;
  std::vector<Stats::PrimitiveCounterSnapshot> host_counters_;
  std::vector<Stats::PrimitiveGaugeSnapshot> host_gauges_;
  SystemTime snapshot_time_;
};

} // namespace Server
} // namespace Envoy
