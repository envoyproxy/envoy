#include "server/server.h"

#include <signal.h>

#include <cstdint>
#include <functional>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/api/api_impl.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/local_info/local_info_impl.h"
#include "common/memory/stats.h"
#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"
#include "common/router/rds_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/statsd.h"
#include "common/upstream/cluster_manager_impl.h"

#include "server/configuration_impl.h"
#include "server/connection_handler_impl.h"
#include "server/guarddog_impl.h"
#include "server/test_hooks.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace Server {

InstanceImpl::InstanceImpl(Options& options, Network::Address::InstanceConstSharedPtr local_address,
                           TestHooks& hooks, HotRestart& restarter, Stats::StoreRoot& store,
                           Thread::BasicLockable& access_log_lock,
                           ComponentFactory& component_factory, ThreadLocal::Instance& tls)
    : options_(options), restarter_(restarter), start_time_(time(nullptr)),
      original_start_time_(start_time_),
      stats_store_(store), server_stats_{ALL_SERVER_STATS(
                               POOL_GAUGE_PREFIX(stats_store_, "server."))},
      thread_local_(tls), api_(new Api::Impl(options.fileFlushIntervalMsec())),
      dispatcher_(api_->allocateDispatcher()),
      handler_(new ConnectionHandlerImpl(log(), *dispatcher_)), listener_component_factory_(*this),
      worker_factory_(thread_local_, *api_, hooks),
      dns_resolver_(dispatcher_->createDnsResolver({})),
      access_log_manager_(*api_, *dispatcher_, access_log_lock, store) {

  failHealthcheck(false);

  uint64_t version_int;
  if (!StringUtil::atoul(VersionInfo::revision().substr(0, 6).c_str(), version_int, 16)) {
    throw EnvoyException("compiled GIT SHA is invalid. Invalid build.");
  }
  server_stats_.version_.set(version_int);

  restarter_.initialize(*dispatcher_, *this);
  drain_manager_ = component_factory.createDrainManager(*this);

  try {
    initialize(options, local_address, component_factory);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(critical, "error initializing configuration '{}': {}",
              options.configPath() +
                  (options.bootstrapPath().empty() ? "" : (";" + options.bootstrapPath())),
              e.what());
    thread_local_.shutdownGlobalThreading();
    thread_local_.shutdownThread();
    exit(1);
  }
}

InstanceImpl::~InstanceImpl() { restarter_.shutdown(); }

Upstream::ClusterManager& InstanceImpl::clusterManager() { return config_->clusterManager(); }

Tracing::HttpTracer& InstanceImpl::httpTracer() { return config_->httpTracer(); }

void InstanceImpl::drainListeners() {
  ENVOY_LOG(warn, "closing and draining listeners");
  listener_manager_->stopListeners();
  drain_manager_->startDrainSequence(nullptr);
}

void InstanceImpl::failHealthcheck(bool fail) {
  // We keep liveness state in shared memory so the parent process sees the same state.
  server_stats_.live_.set(!fail);
}

void InstanceUtil::flushCountersAndGaugesToSinks(const std::list<Stats::SinkPtr>& sinks,
                                                 Stats::Store& store) {
  for (const auto& sink : sinks) {
    sink->beginFlush();
  }

  for (const Stats::CounterSharedPtr& counter : store.counters()) {
    uint64_t delta = counter->latch();
    if (counter->used()) {
      for (const auto& sink : sinks) {
        sink->flushCounter(counter->name(), delta);
      }
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : store.gauges()) {
    if (gauge->used()) {
      for (const auto& sink : sinks) {
        sink->flushGauge(gauge->name(), gauge->value());
      }
    }
  }

  for (const auto& sink : sinks) {
    sink->endFlush();
  }
}

void InstanceImpl::flushStats() {
  ENVOY_LOG(debug, "flushing stats");
  HotRestart::GetParentStatsInfo info;
  restarter_.getParentStats(info);
  server_stats_.uptime_.set(time(nullptr) - original_start_time_);
  server_stats_.memory_allocated_.set(Memory::Stats::totalCurrentlyAllocated() +
                                      info.memory_allocated_);
  server_stats_.memory_heap_size_.set(Memory::Stats::totalCurrentlyReserved());
  server_stats_.parent_connections_.set(info.num_connections_);
  server_stats_.total_connections_.set(numConnections() + info.num_connections_);
  server_stats_.days_until_first_cert_expiring_.set(
      sslContextManager().daysUntilFirstCertExpires());

  InstanceUtil::flushCountersAndGaugesToSinks(stat_sinks_, stats_store_);
  stat_flush_timer_->enableTimer(config_->statsFlushInterval());
}

void InstanceImpl::getParentStats(HotRestart::GetParentStatsInfo& info) {
  info.memory_allocated_ = Memory::Stats::totalCurrentlyAllocated();
  info.num_connections_ = numConnections();
}

bool InstanceImpl::healthCheckFailed() { return server_stats_.live_.value() == 0; }

void InstanceImpl::initialize(Options& options,
                              Network::Address::InstanceConstSharedPtr local_address,
                              ComponentFactory& component_factory) {
  ENVOY_LOG(warn, "initializing epoch {} (hot restart version={})", options.restartEpoch(),
            restarter_.version());

  // Handle configuration that needs to take place prior to the main configuration load.
  Json::ObjectSharedPtr config_json = Json::Factory::loadFromFile(options.configPath());
  envoy::api::v2::Bootstrap bootstrap;
  if (!options.bootstrapPath().empty()) {
    MessageUtil::loadFromFile(options.bootstrapPath(), bootstrap);
  }
  bootstrap.mutable_node()->set_build_version(VersionInfo::version());

  local_info_.reset(
      new LocalInfo::LocalInfoImpl(bootstrap.node(), local_address, options.serviceZone(),
                                   options.serviceClusterName(), options.serviceNodeName()));

  Configuration::InitialImpl initial_config(*config_json);
  ENVOY_LOG(info, "admin address: {}", initial_config.admin().address()->asString());

  HotRestart::ShutdownParentAdminInfo info;
  info.original_start_time_ = original_start_time_;
  restarter_.shutdownParentAdmin(info);
  original_start_time_ = info.original_start_time_;
  admin_.reset(new AdminImpl(initial_config.admin().accessLogPath(),
                             initial_config.admin().profilePath(), options.adminAddressPath(),
                             initial_config.admin().address(), *this));

  admin_scope_ = stats_store_.createScope("listener.admin.");
  handler_->addListener(*admin_, admin_->mutable_socket(), *admin_scope_, 0,
                        Network::ListenerOptions::listenerOptionsWithBindToPort());

  loadServerFlags(initial_config.flagsPath());

  // Workers get created first so they register for thread local updates.
  listener_manager_.reset(
      new ListenerManagerImpl(*this, listener_component_factory_, worker_factory_));

  // The main thread is also registered for thread local updates so that code that does not care
  // whether it runs on the main thread or on workers can still use TLS.
  thread_local_.registerThread(*dispatcher_, true);

  // We can now initialize stats for threading.
  stats_store_.initializeThreading(*dispatcher_, thread_local_);

  // Runtime gets initialized before the main configuration since during main configuration
  // load things may grab a reference to the loader for later use.
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);

  // Once we have runtime we can initialize the SSL context manager.
  ssl_context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_loader_));

  cluster_manager_factory_.reset(new Upstream::ProdClusterManagerFactory(
      runtime(), stats(), threadLocal(), random(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo()));

  route_config_provider_manager_.reset(new Router::RouteConfigProviderManagerImpl(
      runtime(), dispatcher(), random(), localInfo(), threadLocal()));

  // Now the configuration gets parsed. The configuration may start setting thread local data
  // per above. See MainImpl::initialize() for why we do this pointer dance.
  Configuration::MainImpl* main_config = new Configuration::MainImpl();
  config_.reset(main_config);
  main_config->initialize(*config_json, bootstrap, *this, *cluster_manager_factory_);

  // Setup signals.
  sigterm_ = dispatcher_->listenForSignal(SIGTERM, [this]() -> void {
    ENVOY_LOG(warn, "caught SIGTERM");
    restarter_.terminateParent();
    dispatcher_->exit();
  });

  sig_usr_1_ = dispatcher_->listenForSignal(SIGUSR1, [this]() -> void {
    ENVOY_LOG(warn, "caught SIGUSR1");
    access_log_manager_.reopen();
  });

  sig_hup_ = dispatcher_->listenForSignal(SIGHUP, []() -> void {
    ENVOY_LOG(warn, "caught and eating SIGHUP. See documentation for how to hot restart.");
  });

  initializeStatSinks();

  // Some of the stat sinks may need dispatcher support so don't flush until the main loop starts.
  // Just setup the timer.
  stat_flush_timer_ = dispatcher_->createTimer([this]() -> void { flushStats(); });
  stat_flush_timer_->enableTimer(config_->statsFlushInterval());

  // GuardDog (deadlock detection) object and thread setup before workers are
  // started and before our own run() loop runs.
  guard_dog_.reset(
      new Server::GuardDogImpl(stats_store_, *config_, ProdMonotonicTimeSource::instance_));
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

    return Runtime::LoaderPtr{new Runtime::LoaderImpl(
        server.dispatcher(), server.threadLocal(), config.runtime()->symlinkRoot(),
        config.runtime()->subdirectory(), override_subdirectory, server.stats(), server.random())};
  } else {
    return Runtime::LoaderPtr{new Runtime::NullLoaderImpl(server.random())};
  }
}

void InstanceImpl::initializeStatSinks() {
  if (config_->statsdUdpIpAddress().valid()) {
    ENVOY_LOG(info, "statsd UDP ip address: {}", config_->statsdUdpIpAddress().value());
    stat_sinks_.emplace_back(new Stats::Statsd::UdpStatsdSink(
        thread_local_,
        Network::Utility::parseInternetAddressAndPort(config_->statsdUdpIpAddress().value())));
    stats_store_.addSink(*stat_sinks_.back());
  } else if (config_->statsdUdpPort().valid()) {
    // TODO(hennna): DEPRECATED - statsdUdpPort will be removed in 1.4.0.
    ENVOY_LOG(warn, "statsd_local_udp_port has been DEPRECATED and will be removed in 1.4.0. "
                    "Consider setting statsd_udp_ip_address instead.");
    ENVOY_LOG(info, "statsd UDP port: {}", config_->statsdUdpPort().value());
    Network::Address::InstanceConstSharedPtr address(
        new Network::Address::Ipv4Instance(config_->statsdUdpPort().value()));
    stat_sinks_.emplace_back(new Stats::Statsd::UdpStatsdSink(thread_local_, address));
    stats_store_.addSink(*stat_sinks_.back());
  }

  if (config_->statsdTcpClusterName().valid()) {
    ENVOY_LOG(info, "statsd TCP cluster: {}", config_->statsdTcpClusterName().value());
    stat_sinks_.emplace_back(
        new Stats::Statsd::TcpStatsdSink(*local_info_, config_->statsdTcpClusterName().value(),
                                         thread_local_, config_->clusterManager(), stats_store_));
    stats_store_.addSink(*stat_sinks_.back());
  }
}

void InstanceImpl::loadServerFlags(const Optional<std::string>& flags_path) {
  if (!flags_path.valid()) {
    return;
  }

  ENVOY_LOG(info, "server flags path: {}", flags_path.value());
  if (api_->fileExists(flags_path.value() + "/drain")) {
    ENVOY_LOG(warn, "starting server in drain mode");
    failHealthcheck(true);
  }
}

uint64_t InstanceImpl::numConnections() { return listener_manager_->numConnections(); }

void InstanceImpl::run() {
  // Register for cluster manager init notification. We don't start serving worker traffic until
  // upstream clusters are initialized which may involve running the event loop. Note however that
  // this can fire immediately if all clusters have already initialized.
  clusterManager().setInitializedCb([this]() -> void {
    ENVOY_LOG(warn, "all clusters initialized. initializing init manager");
    init_manager_.initialize([this]() -> void { startWorkers(); });
  });

  // Run the main dispatch loop waiting to exit.
  ENVOY_LOG(warn, "starting main dispatch loop");
  auto watchdog = guard_dog_->createWatchDog(Thread::Thread::currentThreadId());
  watchdog->startWatchdog(*dispatcher_);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ENVOY_LOG(warn, "main dispatch loop exited");
  guard_dog_->stopWatching(watchdog);
  watchdog.reset();

  // Before starting to shutdown anything else, stop slot destruction updates.
  thread_local_.shutdownGlobalThreading();

  // Before the workers start exiting we should disable stat threading.
  stats_store_.shutdownThreading();

  // Shutdown all the workers now that the main dispatch loop is done.
  listener_manager_->stopWorkers();

  // Only flush if we have not been hot restarted.
  if (stat_flush_timer_) {
    flushStats();
  }

  config_->clusterManager().shutdown();
  handler_.reset();
  thread_local_.shutdownThread();
  ENVOY_LOG(warn, "exiting");
  log().flush();
}

Runtime::Loader& InstanceImpl::runtime() { return *runtime_loader_; }

void InstanceImpl::shutdown() {
  ENVOY_LOG(warn, "shutdown invoked. sending SIGTERM to self");
  kill(getpid(), SIGTERM);
}

void InstanceImpl::shutdownAdmin() {
  ENVOY_LOG(warn, "shutting down admin due to child startup");
  stat_flush_timer_.reset();
  handler_->stopListeners();
  admin_->mutable_socket().close();

  ENVOY_LOG(warn, "terminating parent process");
  restarter_.terminateParent();
}

} // namespace Server
} // namespace Envoy
