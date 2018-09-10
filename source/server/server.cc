#include "server/server.h"

#include <signal.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/config/bootstrap/v2//bootstrap.pb.validate.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/api/api_impl.h"
#include "common/api/os_sys_calls_impl.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/config/bootstrap_json.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/local_info/local_info_impl.h"
#include "common/memory/stats.h"
#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"
#include "common/router/rds_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/singleton/manager_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/upstream/cluster_manager_impl.h"

#include "server/configuration_impl.h"
#include "server/connection_handler_impl.h"
#include "server/guarddog_impl.h"
#include "server/test_hooks.h"

namespace Envoy {
namespace Server {

InstanceImpl::InstanceImpl(Options& options, Event::TimeSystem& time_system,
                           Network::Address::InstanceConstSharedPtr local_address, TestHooks& hooks,
                           HotRestart& restarter, Stats::StoreRoot& store,
                           Thread::BasicLockable& access_log_lock,
                           ComponentFactory& component_factory,
                           Runtime::RandomGeneratorPtr&& random_generator,
                           ThreadLocal::Instance& tls)
    : options_(options), time_system_(time_system), restarter_(restarter),
      start_time_(time(nullptr)), original_start_time_(start_time_), stats_store_(store),
      thread_local_(tls), api_(new Api::Impl(options.fileFlushIntervalMsec())),
      dispatcher_(api_->allocateDispatcher(time_system)),
      singleton_manager_(new Singleton::ManagerImpl()),
      handler_(new ConnectionHandlerImpl(ENVOY_LOGGER(), *dispatcher_)),
      random_generator_(std::move(random_generator)),
      secret_manager_(std::make_unique<Secret::SecretManagerImpl>()),
      listener_component_factory_(*this), worker_factory_(thread_local_, *api_, hooks, time_system),
      dns_resolver_(dispatcher_->createDnsResolver({})),
      access_log_manager_(*api_, *dispatcher_, access_log_lock, store), terminated_(false) {

  try {
    if (!options.logPath().empty()) {
      try {
        file_logger_ = std::make_unique<Logger::FileSinkDelegate>(
            options.logPath(), access_log_manager_, Logger::Registry::getSink());
      } catch (const EnvoyException& e) {
        throw EnvoyException(
            fmt::format("Failed to open log-file '{}'. e.what(): {}", options.logPath(), e.what()));
      }
    }

    restarter_.initialize(*dispatcher_, *this);
    drain_manager_ = component_factory.createDrainManager(*this);
    initialize(options, local_address, component_factory);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(critical, "error initializing configuration '{}': {}", options.configPath(),
              e.what());
    terminate();
    throw;
  } catch (const std::exception& e) {
    ENVOY_LOG(critical, "error initializing due to unexpected exception: {}", e.what());
    terminate();
    throw;
  } catch (...) {
    ENVOY_LOG(critical, "error initializing due to unknown exception");
    terminate();
    throw;
  }
}

InstanceImpl::~InstanceImpl() {
  terminate();

  // Stop logging to file before all the AccessLogManager and its dependencies are
  // destructed to avoid crashing at shutdown.
  file_logger_.reset();

  // Destruct the ListenerManager explicitly, before InstanceImpl's local init_manager_ is
  // destructed.
  //
  // The ListenerManager's DestinationPortsMap contains FilterChainSharedPtrs. There is a rare race
  // condition where one of these FilterChains contains an HttpConnectionManager, which contains an
  // RdsRouteConfigProvider, which contains an RdsRouteConfigSubscriptionSharedPtr. Since
  // RdsRouteConfigSubscription is an Init::Target, ~RdsRouteConfigSubscription triggers a callback
  // set at initialization, which goes to unregister it from the top-level InitManager, which has
  // already been destructed (use-after-free) causing a segfault.
  listener_manager_.reset();
}

Upstream::ClusterManager& InstanceImpl::clusterManager() { return *config_->clusterManager(); }

Tracing::HttpTracer& InstanceImpl::httpTracer() { return config_->httpTracer(); }

void InstanceImpl::drainListeners() {
  ENVOY_LOG(info, "closing and draining listeners");
  listener_manager_->stopListeners();
  drain_manager_->startDrainSequence(nullptr);
}

void InstanceImpl::failHealthcheck(bool fail) {
  // We keep liveness state in shared memory so the parent process sees the same state.
  server_stats_->live_.set(!fail);
}

void InstanceUtil::flushMetricsToSinks(const std::list<Stats::SinkPtr>& sinks,
                                       Stats::Source& source) {
  for (const auto& sink : sinks) {
    sink->flush(source);
  }
  // TODO(mrice32): this reset should be called by the StoreRoot on stat construction/destruction so
  // that it doesn't need to be reset when the set of stats isn't changing.
  source.clearCache();
}

void InstanceImpl::flushStats() {
  ENVOY_LOG(debug, "flushing stats");
  // A shutdown initiated before this callback may prevent this from being called as per
  // the semantics documented in ThreadLocal's runOnAllThreads method.
  stats_store_.mergeHistograms([this]() -> void {
    HotRestart::GetParentStatsInfo info;
    restarter_.getParentStats(info);
    server_stats_->uptime_.set(time(nullptr) - original_start_time_);
    server_stats_->memory_allocated_.set(Memory::Stats::totalCurrentlyAllocated() +
                                         info.memory_allocated_);
    server_stats_->memory_heap_size_.set(Memory::Stats::totalCurrentlyReserved());
    server_stats_->parent_connections_.set(info.num_connections_);
    server_stats_->total_connections_.set(numConnections() + info.num_connections_);
    server_stats_->days_until_first_cert_expiring_.set(
        sslContextManager().daysUntilFirstCertExpires());
    InstanceUtil::flushMetricsToSinks(config_->statsSinks(), stats_store_.source());
    // TODO(ramaraochavali): consider adding different flush interval for histograms.
    if (stat_flush_timer_ != nullptr) {
      stat_flush_timer_->enableTimer(config_->statsFlushInterval());
    }
  });
}

void InstanceImpl::getParentStats(HotRestart::GetParentStatsInfo& info) {
  info.memory_allocated_ = Memory::Stats::totalCurrentlyAllocated();
  info.num_connections_ = numConnections();
}

bool InstanceImpl::healthCheckFailed() { return server_stats_->live_.value() == 0; }

InstanceUtil::BootstrapVersion
InstanceUtil::loadBootstrapConfig(envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                                  Options& options) {
  try {
    if (!options.configPath().empty()) {
      MessageUtil::loadFromFile(options.configPath(), bootstrap);
    }
    if (!options.configYaml().empty()) {
      envoy::config::bootstrap::v2::Bootstrap bootstrap_override;
      MessageUtil::loadFromYaml(options.configYaml(), bootstrap_override);
      bootstrap.MergeFrom(bootstrap_override);
    }
    MessageUtil::validate(bootstrap);
    return BootstrapVersion::V2;
  } catch (const EnvoyException& e) {
    if (options.v2ConfigOnly()) {
      throw;
    }
    // TODO(htuch): When v1 is deprecated, make this a warning encouraging config upgrade.
    ENVOY_LOG(debug, "Unable to initialize config as v2, will retry as v1: {}", e.what());
  }
  if (!options.configYaml().empty()) {
    throw EnvoyException("V1 config (detected) with --config-yaml is not supported");
  }
  Json::ObjectSharedPtr config_json = Json::Factory::loadFromFile(options.configPath());
  Config::BootstrapJson::translateBootstrap(*config_json, bootstrap, options.statsOptions());
  MessageUtil::validate(bootstrap);
  return BootstrapVersion::V1;
}

void InstanceImpl::initialize(Options& options,
                              Network::Address::InstanceConstSharedPtr local_address,
                              ComponentFactory& component_factory) {
  ENVOY_LOG(info, "initializing epoch {} (hot restart version={})", options.restartEpoch(),
            restarter_.version());

  ENVOY_LOG(info, "statically linked extensions:");
  ENVOY_LOG(info, "  access_loggers: {}",
            Registry::FactoryRegistry<Configuration::AccessLogInstanceFactory>::allFactoryNames());
  ENVOY_LOG(
      info, "  filters.http: {}",
      Registry::FactoryRegistry<Configuration::NamedHttpFilterConfigFactory>::allFactoryNames());
  ENVOY_LOG(info, "  filters.listener: {}",
            Registry::FactoryRegistry<
                Configuration::NamedListenerFilterConfigFactory>::allFactoryNames());
  ENVOY_LOG(
      info, "  filters.network: {}",
      Registry::FactoryRegistry<Configuration::NamedNetworkFilterConfigFactory>::allFactoryNames());
  ENVOY_LOG(info, "  stat_sinks: {}",
            Registry::FactoryRegistry<Configuration::StatsSinkFactory>::allFactoryNames());
  ENVOY_LOG(info, "  tracers: {}",
            Registry::FactoryRegistry<Configuration::TracerFactory>::allFactoryNames());
  ENVOY_LOG(info, "  transport_sockets.downstream: {}",
            Registry::FactoryRegistry<
                Configuration::DownstreamTransportSocketConfigFactory>::allFactoryNames());
  ENVOY_LOG(info, "  transport_sockets.upstream: {}",
            Registry::FactoryRegistry<
                Configuration::UpstreamTransportSocketConfigFactory>::allFactoryNames());

  // Handle configuration that needs to take place prior to the main configuration load.
  InstanceUtil::loadBootstrapConfig(bootstrap_, options);
  bootstrap_config_update_time_ = time_system_.systemTime();

  // Needs to happen as early as possible in the instantiation to preempt the objects that require
  // stats.
  stats_store_.setTagProducer(Config::Utility::createTagProducer(bootstrap_));

  server_stats_.reset(
      new ServerStats{ALL_SERVER_STATS(POOL_GAUGE_PREFIX(stats_store_, "server."))});

  server_stats_->concurrency_.set(options_.concurrency());
  server_stats_->hot_restart_epoch_.set(options_.restartEpoch());

  failHealthcheck(false);

  uint64_t version_int;
  if (!StringUtil::atoul(VersionInfo::revision().substr(0, 6).c_str(), version_int, 16)) {
    throw EnvoyException("compiled GIT SHA is invalid. Invalid build.");
  }

  server_stats_->version_.set(version_int);
  bootstrap_.mutable_node()->set_build_version(VersionInfo::version());

  local_info_.reset(
      new LocalInfo::LocalInfoImpl(bootstrap_.node(), local_address, options.serviceZone(),
                                   options.serviceClusterName(), options.serviceNodeName()));

  Configuration::InitialImpl initial_config(bootstrap_);
  ENVOY_LOG(debug, "admin address: {}", initial_config.admin().address()->asString());

  HotRestart::ShutdownParentAdminInfo info;
  info.original_start_time_ = original_start_time_;
  restarter_.shutdownParentAdmin(info);
  original_start_time_ = info.original_start_time_;
  admin_.reset(new AdminImpl(initial_config.admin().accessLogPath(),
                             initial_config.admin().profilePath(), options.adminAddressPath(),
                             initial_config.admin().address(), *this,
                             stats_store_.createScope("listener.admin.")));
  config_tracker_entry_ =
      admin_->getConfigTracker().add("bootstrap", [this] { return dumpBootstrapConfig(); });
  handler_->addListener(admin_->listener());

  loadServerFlags(initial_config.flagsPath());

  // Workers get created first so they register for thread local updates.
  listener_manager_.reset(
      new ListenerManagerImpl(*this, listener_component_factory_, worker_factory_, time_system_));

  // The main thread is also registered for thread local updates so that code that does not care
  // whether it runs on the main thread or on workers can still use TLS.
  thread_local_.registerThread(*dispatcher_, true);

  // Initialize the overload manager early so other modules can register for actions.
  overload_manager_.reset(
      new OverloadManagerImpl(dispatcher(), stats(), threadLocal(), bootstrap_.overload_manager()));

  // We can now initialize stats for threading.
  stats_store_.initializeThreading(*dispatcher_, thread_local_);

  // Runtime gets initialized before the main configuration since during main configuration
  // load things may grab a reference to the loader for later use.
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);

  // Once we have runtime we can initialize the SSL context manager.
  ssl_context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_loader_));

  cluster_manager_factory_.reset(new Upstream::ProdClusterManagerFactory(
      runtime(), stats(), threadLocal(), random(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo(), secretManager()));

  // Now the configuration gets parsed. The configuration may start setting thread local data
  // per above. See MainImpl::initialize() for why we do this pointer dance.
  Configuration::MainImpl* main_config = new Configuration::MainImpl();
  config_.reset(main_config);
  main_config->initialize(bootstrap_, *this, *cluster_manager_factory_);

  // Instruct the listener manager to create the LDS provider if needed. This must be done later
  // because various items do not yet exist when the listener manager is created.
  if (bootstrap_.dynamic_resources().has_lds_config()) {
    listener_manager_->createLdsApi(bootstrap_.dynamic_resources().lds_config());
  }

  if (bootstrap_.has_hds_config()) {
    const auto& hds_config = bootstrap_.hds_config();
    async_client_manager_ = std::make_unique<Grpc::AsyncClientManagerImpl>(
        clusterManager(), thread_local_, time_system_);
    hds_delegate_.reset(new Upstream::HdsDelegate(
        bootstrap_.node(), stats(),
        Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_, hds_config, stats())
            ->create(),
        dispatcher(), runtime(), stats(), sslContextManager(), random(), info_factory_,
        access_log_manager_, clusterManager(), localInfo()));
  }

  for (Stats::SinkPtr& sink : main_config->statsSinks()) {
    stats_store_.addSink(*sink);
  }

  // Some of the stat sinks may need dispatcher support so don't flush until the main loop starts.
  // Just setup the timer.
  stat_flush_timer_ = dispatcher_->createTimer([this]() -> void { flushStats(); });
  stat_flush_timer_->enableTimer(config_->statsFlushInterval());

  // GuardDog (deadlock detection) object and thread setup before workers are
  // started and before our own run() loop runs.
  guard_dog_.reset(new Server::GuardDogImpl(stats_store_, *config_, time_system_));
}

void InstanceImpl::startWorkers() {
  listener_manager_->startWorkers(*guard_dog_);

  // At this point we are ready to take traffic and all listening ports are up. Notify our parent
  // if applicable that they can stop listening and drain.
  restarter_.drainParentListeners();
  drain_manager_->startParentShutdownSequence();
}

Runtime::LoaderPtr InstanceUtil::createRuntime(Instance& server,
                                               Server::Configuration::Initial& config) {
  if (config.runtime()) {
    ENVOY_LOG(info, "runtime symlink: {}", config.runtime()->symlinkRoot());
    ENVOY_LOG(info, "runtime subdirectory: {}", config.runtime()->subdirectory());

    std::string override_subdirectory =
        config.runtime()->overrideSubdirectory() + "/" + server.localInfo().clusterName();
    ENVOY_LOG(info, "runtime override subdirectory: {}", override_subdirectory);

    Api::OsSysCallsPtr os_sys_calls(new Api::OsSysCallsImpl);
    return std::make_unique<Runtime::DiskBackedLoaderImpl>(
        server.dispatcher(), server.threadLocal(), config.runtime()->symlinkRoot(),
        config.runtime()->subdirectory(), override_subdirectory, server.stats(), server.random(),
        std::move(os_sys_calls));
  } else {
    return std::make_unique<Runtime::LoaderImpl>(server.random(), server.stats(),
                                                 server.threadLocal());
  }
}

void InstanceImpl::loadServerFlags(const absl::optional<std::string>& flags_path) {
  if (!flags_path) {
    return;
  }

  ENVOY_LOG(info, "server flags path: {}", flags_path.value());
  if (api_->fileExists(flags_path.value() + "/drain")) {
    ENVOY_LOG(info, "starting server in drain mode");
    failHealthcheck(true);
  }
}

uint64_t InstanceImpl::numConnections() { return listener_manager_->numConnections(); }

RunHelper::RunHelper(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm,
                     HotRestart& hot_restart, AccessLog::AccessLogManager& access_log_manager,
                     InitManagerImpl& init_manager, OverloadManager& overload_manager,
                     std::function<void()> workers_start_cb) {

  // Setup signals.
  sigterm_ = dispatcher.listenForSignal(SIGTERM, [this, &hot_restart, &dispatcher]() {
    ENVOY_LOG(warn, "caught SIGTERM");
    shutdown(dispatcher, hot_restart);
  });

  sig_usr_1_ = dispatcher.listenForSignal(SIGUSR1, [&access_log_manager]() {
    ENVOY_LOG(warn, "caught SIGUSR1");
    access_log_manager.reopen();
  });

  sig_hup_ = dispatcher.listenForSignal(SIGHUP, []() {
    ENVOY_LOG(warn, "caught and eating SIGHUP. See documentation for how to hot restart.");
  });

  // Register for cluster manager init notification. We don't start serving worker traffic until
  // upstream clusters are initialized which may involve running the event loop. Note however that
  // this can fire immediately if all clusters have already initialized. Also note that we need
  // to guard against shutdown at two different levels since SIGTERM can come in once the run loop
  // starts.
  cm.setInitializedCb([this, &init_manager, &cm, workers_start_cb]() {
    if (shutdown_) {
      return;
    }

    // Pause RDS to ensure that we don't send any requests until we've
    // subscribed to all the RDS resources. The subscriptions happen in the init callbacks,
    // so we pause RDS until we've completed all the callbacks.
    cm.adsMux().pause(Config::TypeUrl::get().RouteConfiguration);

    ENVOY_LOG(info, "all clusters initialized. initializing init manager");
    init_manager.initialize([this, workers_start_cb]() {
      if (shutdown_) {
        return;
      }

      workers_start_cb();
    });

    // Now that we're execute all the init callbacks we can resume RDS
    // as we've subscribed to all the statically defined RDS resources.
    cm.adsMux().resume(Config::TypeUrl::get().RouteConfiguration);
  });

  overload_manager.start();
}

void RunHelper::shutdown(Event::Dispatcher& dispatcher, HotRestart& hot_restart) {
  shutdown_ = true;
  hot_restart.terminateParent();
  dispatcher.exit();
}

void InstanceImpl::run() {
  // We need the RunHelper to be available to call from InstanceImpl::shutdown() below, so
  // we save it as a member variable.
  run_helper_ = std::make_unique<RunHelper>(*dispatcher_, clusterManager(), restarter_,
                                            access_log_manager_, init_manager_, overloadManager(),
                                            [this]() -> void { startWorkers(); });

  // Run the main dispatch loop waiting to exit.
  ENVOY_LOG(info, "starting main dispatch loop");
  auto watchdog = guard_dog_->createWatchDog(Thread::Thread::currentThreadId());
  watchdog->startWatchdog(*dispatcher_);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ENVOY_LOG(info, "main dispatch loop exited");
  guard_dog_->stopWatching(watchdog);
  watchdog.reset();

  terminate();
  run_helper_.reset();
}

void InstanceImpl::terminate() {
  if (terminated_) {
    return;
  }
  terminated_ = true;

  // Before starting to shutdown anything else, stop slot destruction updates.
  thread_local_.shutdownGlobalThreading();

  // Before the workers start exiting we should disable stat threading.
  stats_store_.shutdownThreading();

  // Shutdown all the workers now that the main dispatch loop is done.
  if (listener_manager_.get() != nullptr) {
    listener_manager_->stopWorkers();
  }

  // Only flush if we have not been hot restarted.
  if (stat_flush_timer_) {
    flushStats();
  }

  if (config_.get() != nullptr && config_->clusterManager() != nullptr) {
    config_->clusterManager()->shutdown();
  }
  handler_.reset();
  thread_local_.shutdownThread();
  restarter_.shutdown();
  ENVOY_LOG(info, "exiting");
  ENVOY_FLUSH_LOG();
}

Runtime::Loader& InstanceImpl::runtime() { return *runtime_loader_; }

void InstanceImpl::shutdown() {
  ASSERT(run_helper_.get() != nullptr);
  run_helper_->shutdown(*dispatcher_, restarter_);
}

void InstanceImpl::shutdownAdmin() {
  ENVOY_LOG(warn, "shutting down admin due to child startup");
  // TODO(mattklein123): Since histograms are not shared between processes, this will also stop
  //                     histogram flushing. In the future we can consider whether we want to
  //                     somehow keep flushing histograms from the old process.
  stat_flush_timer_.reset();
  handler_->stopListeners();
  admin_->mutable_socket().close();

  ENVOY_LOG(warn, "terminating parent process");
  restarter_.terminateParent();
}

ProtobufTypes::MessagePtr InstanceImpl::dumpBootstrapConfig() {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::BootstrapConfigDump>();
  config_dump->mutable_bootstrap()->MergeFrom(bootstrap_);
  TimestampUtil::systemClockToTimestamp(bootstrap_config_update_time_,
                                        *(config_dump->mutable_last_updated()));
  return config_dump;
}

} // namespace Server
} // namespace Envoy
