#include "library/common/internal_engine.h"

#include <sys/resource.h>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/utility.h"
#include "source/common/http/http_server_properties_cache_manager_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/synchronization/notification.h"
#include "library/common/stats/utility.h"

namespace Envoy {
namespace {
constexpr absl::Duration ENGINE_RUNNING_TIMEOUT = absl::Seconds(30);
// Google DNS address used for IPv6 probes.
constexpr absl::string_view IPV6_PROBE_ADDRESS = "2001:4860:4860::8888";
constexpr uint32_t IPV6_PROBE_PORT = 53;
} // namespace

static std::atomic<envoy_stream_t> current_stream_handle_{0};

InternalEngine::InternalEngine(std::unique_ptr<EngineCallbacks> callbacks,
                               std::unique_ptr<EnvoyLogger> logger,
                               std::unique_ptr<EnvoyEventTracker> event_tracker,
                               absl::optional<int> thread_priority,
                               Thread::PosixThreadFactoryPtr thread_factory)
    : thread_factory_(std::move(thread_factory)), callbacks_(std::move(callbacks)),
      logger_(std::move(logger)), event_tracker_(std::move(event_tracker)),
      thread_priority_(thread_priority),
      dispatcher_(std::make_unique<Event::ProvisionalDispatcher>()) {
  ExtensionRegistry::registerFactories();

  Api::External::registerApi(std::string(ENVOY_EVENT_TRACKER_API_NAME), &event_tracker_);
}

InternalEngine::InternalEngine(std::unique_ptr<EngineCallbacks> callbacks,
                               std::unique_ptr<EnvoyLogger> logger,
                               std::unique_ptr<EnvoyEventTracker> event_tracker,
                               absl::optional<int> thread_priority)
    : InternalEngine(std::move(callbacks), std::move(logger), std::move(event_tracker),
                     thread_priority, Thread::PosixThreadFactory::create()) {}

envoy_stream_t InternalEngine::initStream() { return current_stream_handle_++; }

envoy_status_t InternalEngine::startStream(envoy_stream_t stream,
                                           EnvoyStreamCallbacks&& stream_callbacks,
                                           bool explicit_flow_control) {
  return dispatcher_->post(
      [&, stream, stream_callbacks = std::move(stream_callbacks), explicit_flow_control]() mutable {
        http_client_->startStream(stream, std::move(stream_callbacks), explicit_flow_control);
      });
}

envoy_status_t InternalEngine::sendHeaders(envoy_stream_t stream, Http::RequestHeaderMapPtr headers,
                                           bool end_stream, bool idempotent) {
  return dispatcher_->post(
      [this, stream, headers = std::move(headers), end_stream, idempotent]() mutable {
        http_client_->sendHeaders(stream, std::move(headers), end_stream, idempotent);
      });
  return ENVOY_SUCCESS;
}

envoy_status_t InternalEngine::readData(envoy_stream_t stream, size_t bytes_to_read) {
  return dispatcher_->post(
      [&, stream, bytes_to_read]() { http_client_->readData(stream, bytes_to_read); });
}

envoy_status_t InternalEngine::sendData(envoy_stream_t stream, Buffer::InstancePtr buffer,
                                        bool end_stream) {
  return dispatcher_->post([&, stream, buffer = std::move(buffer), end_stream]() mutable {
    http_client_->sendData(stream, std::move(buffer), end_stream);
  });
}

envoy_status_t InternalEngine::sendTrailers(envoy_stream_t stream,
                                            Http::RequestTrailerMapPtr trailers) {
  return dispatcher_->post([&, stream, trailers = std::move(trailers)]() mutable {
    http_client_->sendTrailers(stream, std::move(trailers));
  });
}

envoy_status_t InternalEngine::cancelStream(envoy_stream_t stream) {
  return dispatcher_->post([&, stream]() { http_client_->cancelStream(stream); });
}

// This function takes a `std::shared_ptr` instead of `std::unique_ptr` because `std::function` is a
// copy-constructible type, so it's not possible to move capture `std::unique_ptr` with
// `std::function`.
envoy_status_t InternalEngine::run(std::shared_ptr<Envoy::OptionsImplBase> options) {
  Thread::Options thread_options;
  thread_options.priority_ = thread_priority_;
  main_thread_ = thread_factory_->createThread([this, options]() mutable -> void { main(options); },
                                               thread_options, /* crash_on_failure= */ false);
  return (main_thread_ != nullptr) ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

envoy_status_t InternalEngine::main(std::shared_ptr<Envoy::OptionsImplBase> options) {
  // Using unique_ptr ensures main_common's lifespan is strictly scoped to this function.
  std::unique_ptr<EngineCommon> main_common;
  {
    Thread::LockGuard lock(mutex_);
    TRY_NEEDS_AUDIT {
      if (event_tracker_ != nullptr) {
        assert_handler_registration_ =
            Assert::addDebugAssertionFailureRecordAction([this](const char* location) {
              absl::flat_hash_map<std::string, std::string> event{
                  {{"name", "assertion"}, {"location", std::string(location)}}};
              event_tracker_->on_track_(event);
            });
        bug_handler_registration_ =
            Assert::addEnvoyBugFailureRecordAction([this](const char* location) {
              absl::flat_hash_map<std::string, std::string> event{
                  {{"name", "bug"}, {"location", std::string(location)}}};
              event_tracker_->on_track_(event);
            });
      }

      // We let the thread clean up this log delegate pointer
      if (logger_ != nullptr) {
        log_delegate_ptr_ = std::make_unique<Logger::LambdaDelegate>(std::move(logger_),
                                                                     Logger::Registry::getSink());
      } else {
        log_delegate_ptr_ =
            std::make_unique<Logger::DefaultDelegate>(log_mutex_, Logger::Registry::getSink());
      }

      main_common = std::make_unique<EngineCommon>(options);
      server_ = main_common->server();
      event_dispatcher_ = &server_->dispatcher();

      cv_.notifyAll();
    }
    END_TRY
    CATCH(const Envoy::EnvoyException& e, { PANIC(e.what()); });

    // Note: We're waiting longer than we might otherwise to drain to the main thread's dispatcher.
    // This is because we're not simply waiting for its availability and for it to have started, but
    // also because we're waiting for clusters to have done their first attempt at DNS resolution.
    // When we improve synchronous failure handling and/or move to dynamic forwarding, we only need
    // to wait until the dispatcher is running (and can drain by enqueueing a drain callback on it,
    // as we did previously).

    postinit_callback_handler_ = main_common->server()->lifecycleNotifier().registerCallback(
        Envoy::Server::ServerLifecycleNotifier::Stage::PostInit, [this]() -> void {
          ASSERT(Thread::MainThread::isMainOrTestThread());

          Envoy::Server::GenericFactoryContextImpl generic_context(
              server_->serverFactoryContext(),
              server_->serverFactoryContext().messageValidationVisitor());
          connectivity_manager_ = Network::ConnectivityManagerFactory{generic_context}.get();
          if (Runtime::runtimeFeatureEnabled(
                  "envoy.reloadable_features.dns_cache_set_ip_version_to_remove")) {
            if (!hasIpV6Connectivity()) {
              connectivity_manager_->dnsCache()->setIpVersionToRemove(
                  {Network::Address::IpVersion::v6});
            }
          }
          auto v4_interfaces = connectivity_manager_->enumerateV4Interfaces();
          auto v6_interfaces = connectivity_manager_->enumerateV6Interfaces();
          logInterfaces("netconf_get_v4_interfaces", v4_interfaces);
          logInterfaces("netconf_get_v6_interfaces", v6_interfaces);
          client_scope_ = server_->serverFactoryContext().scope().createScope("pulse.");
          // StatNameSet is lock-free, the benefit of using it is being able to create StatsName
          // on-the-fly without risking contention on system with lots of threads.
          // It also comes with ease of programming.
          stat_name_set_ = client_scope_->symbolTable().makeSet("pulse");
          auto api_listener = server_->listenerManager().apiListener()->get().createHttpApiListener(
              server_->dispatcher());
          ASSERT(api_listener != nullptr);
          http_client_ = std::make_unique<Http::Client>(std::move(api_listener), *dispatcher_,
                                                        server_->serverFactoryContext().scope(),
                                                        server_->api().randomGenerator());
          dispatcher_->drain(server_->dispatcher());
          engine_running_.Notify();
          callbacks_->on_engine_running_();
        });
  } // mutex_

  // The main run loop must run without holding the mutex, so that the destructor can acquire it.
  bool run_success = main_common->run();
  // The above call is blocking; at this point the event loop has exited.

  // Ensure destructors run on Envoy's main thread.
  postinit_callback_handler_.reset(nullptr);
  connectivity_manager_.reset();
  client_scope_.reset();
  stat_name_set_.reset();
  main_common.reset(nullptr);
  bug_handler_registration_.reset(nullptr);
  assert_handler_registration_.reset(nullptr);

  if (event_tracker_ != nullptr) {
    event_tracker_->on_exit_();
  }
  callbacks_->on_exit_();

  return run_success ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

envoy_status_t InternalEngine::terminate() {
  if (terminated_) {
    IS_ENVOY_BUG("attempted to double terminate engine");
    return ENVOY_FAILURE;
  }
  // The Engine could not be created.
  if (main_thread_ == nullptr) {
    return ENVOY_FAILURE;
  }
  // If main_thread_ has finished (or hasn't started), there's nothing more to do.
  if (!main_thread_->joinable()) {
    return ENVOY_FAILURE;
  }

  // Wait until the Engine is ready before calling terminate to avoid assertion failures.
  // TODO(fredyw): Fix this without having to wait.
  ASSERT(engine_running_.WaitForNotificationWithTimeout(ENGINE_RUNNING_TIMEOUT));

  // We need to be sure that MainCommon is finished being constructed so we can dispatch shutdown.
  {
    Thread::LockGuard lock(mutex_);

    if (!event_dispatcher_) {
      cv_.wait(mutex_);
    }

    ASSERT(event_dispatcher_);
    ASSERT(dispatcher_);

    // We must destroy the Http::ApiListener in the main thread.
    dispatcher_->post([this]() { http_client_->shutdownApiListener(); });

    // Exit the event loop and finish up in Engine::run(...)
    if (thread_factory_->currentPthreadId() == main_thread_->pthreadId()) {
      // TODO(goaway): figure out some way to support this.
      PANIC("Terminating the engine from its own main thread is currently unsupported.");
    } else {
      dispatcher_->terminate();
    }
  } // lock(_mutex)

  if (thread_factory_->currentPthreadId() != main_thread_->pthreadId()) {
    main_thread_->join();
  }
  terminated_ = true;
  return ENVOY_SUCCESS;
}

bool InternalEngine::isTerminated() const { return terminated_; }

InternalEngine::~InternalEngine() {
  if (!terminated_) {
    terminate();
  }
}

envoy_status_t InternalEngine::setProxySettings(const char* hostname, const uint16_t port) {
  return dispatcher_->post([&, host = std::string(hostname), port]() -> void {
    connectivity_manager_->setProxySettings(Network::ProxySettings::parseHostAndPort(host, port));
  });
}

envoy_status_t InternalEngine::resetConnectivityState() {
  return dispatcher_->post([&]() -> void { connectivity_manager_->resetConnectivityState(); });
}

void InternalEngine::onDefaultNetworkAvailable() {
  ENVOY_LOG_MISC(trace, "Calling the default network available callback");
}

void InternalEngine::onDefaultNetworkChanged(NetworkType network) {
  ENVOY_LOG_MISC(trace, "Calling the default network changed callback");
  dispatcher_->post([&, network]() -> void {
    envoy_netconf_t configuration = Network::ConnectivityManagerImpl::setPreferredNetwork(network);
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.dns_cache_set_ip_version_to_remove")) {
      // The IP version to remove flag must be set first before refreshing the DNS cache so that
      // the DNS cache will be updated with whether or not the IPv6 addresses will need to be
      // removed.
      if (!hasIpV6Connectivity()) {
        connectivity_manager_->dnsCache()->setIpVersionToRemove({Network::Address::IpVersion::v6});
      } else {
        connectivity_manager_->dnsCache()->setIpVersionToRemove(absl::nullopt);
      }
    }
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.reset_brokenness_on_nework_change")) {
      Http::HttpServerPropertiesCacheManager& cache_manager =
          server_->httpServerPropertiesCacheManager();

      Http::HttpServerPropertiesCacheManager::CacheFn clear_brokenness =
          [](Http::HttpServerPropertiesCache& cache) { cache.resetBrokenness(); };
      cache_manager.forEachThreadLocalCache(clear_brokenness);
    }
    connectivity_manager_->refreshDns(configuration, true);
  });
}

void InternalEngine::onDefaultNetworkUnavailable() {
  ENVOY_LOG_MISC(trace, "Calling the default network unavailable callback");
  dispatcher_->post([&]() -> void { connectivity_manager_->dnsCache()->stop(); });
}

envoy_status_t InternalEngine::recordCounterInc(absl::string_view elements, envoy_stats_tags tags,
                                                uint64_t count) {
  return dispatcher_->post(
      [&, name = Stats::Utility::sanitizeStatsName(elements), tags, count]() -> void {
        ENVOY_LOG(trace, "[pulse.{}] recordCounterInc", name);
        Stats::StatNameTagVector tags_vctr =
            Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
        Stats::Utility::counterFromElements(*client_scope_, {Stats::DynamicName(name)}, tags_vctr)
            .add(count);
      });
}

Event::ProvisionalDispatcher& InternalEngine::dispatcher() const { return *dispatcher_; }

Thread::PosixThreadFactory& InternalEngine::threadFactory() const { return *thread_factory_; }

void statsAsText(const std::map<std::string, uint64_t>& all_stats,
                 const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                 Buffer::Instance& response) {
  for (const auto& stat : all_stats) {
    response.addFragments({stat.first, ": ", absl::StrCat(stat.second), "\n"});
  }
  std::map<std::string, std::string> all_histograms;
  for (const Stats::ParentHistogramSharedPtr& histogram : histograms) {
    if (histogram->used()) {
      all_histograms.emplace(histogram->name(), histogram->quantileSummary());
    }
  }
  for (const auto& histogram : all_histograms) {
    response.addFragments({histogram.first, ": ", histogram.second, "\n"});
  }
}

void handlerStats(Stats::Store& stats, Buffer::Instance& response) {
  std::map<std::string, uint64_t> all_stats;
  for (const Stats::CounterSharedPtr& counter : stats.counters()) {
    if (counter->used()) {
      all_stats.emplace(counter->name(), counter->value());
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : stats.gauges()) {
    if (gauge->used()) {
      all_stats.emplace(gauge->name(), gauge->value());
    }
  }

  std::vector<Stats::ParentHistogramSharedPtr> histograms = stats.histograms();
  stats.symbolTable().sortByStatNames<Stats::ParentHistogramSharedPtr>(
      histograms.begin(), histograms.end(),
      [](const Stats::ParentHistogramSharedPtr& a) -> Stats::StatName { return a->statName(); });

  statsAsText(all_stats, histograms, response);
}

std::string InternalEngine::dumpStats() {
  if (!main_thread_->joinable()) {
    return "";
  }

  std::string stats;
  absl::Notification stats_received;
  if (dispatcher_->post([&]() -> void {
        Envoy::Buffer::OwnedImpl instance;
        handlerStats(server_->stats(), instance);
        stats = instance.toString();
        stats_received.Notify();
      }) == ENVOY_SUCCESS) {
    stats_received.WaitForNotification();
    return stats;
  }
  return stats;
}

Upstream::ClusterManager& InternalEngine::getClusterManager() {
  ASSERT(dispatcher_->isThreadSafe(),
         "getClusterManager must be called from the dispatcher's context");
  return server_->clusterManager();
}

Stats::Store& InternalEngine::getStatsStore() {
  ASSERT(dispatcher_->isThreadSafe(), "getStatsStore must be called from the dispatcher's context");
  return server_->stats();
}

void InternalEngine::logInterfaces(absl::string_view event,
                                   std::vector<Network::InterfacePair>& interfaces) {
  auto all_names_printer = [](std::vector<Network::InterfacePair>& interfaces) -> std::string {
    std::vector<std::string> names;
    names.resize(interfaces.size());
    std::transform(interfaces.begin(), interfaces.end(), names.begin(),
                   [](Network::InterfacePair& pair) { return std::get<0>(pair); });

    auto unique_end = std::unique(names.begin(), names.end());
    std::string all_names = std::accumulate(
        names.begin(), unique_end, std::string{}, [](std::string acc, std::string next) {
          return acc.empty() ? next : std::move(acc) + "," + next;
        });
    return all_names;
  };
  ENVOY_LOG_EVENT(debug, event, "{}", all_names_printer(interfaces));
}

bool InternalEngine::hasIpV6Connectivity() {
  // This probing IPv6 logic is borrowed from Chromium.
  // -
  // https://source.chromium.org/chromium/chromium/src/+/main:net/dns/host_resolver_manager.cc;l=154-157;drc=7b232da0f22e8cdf555d43c52b6491baeb87f729
  // -
  // https://source.chromium.org/chromium/chromium/src/+/main:net/dns/host_resolver_manager.cc;l=1467-1488;drc=7b232da0f22e8cdf555d43c52b6491baeb87f729
  ENVOY_LOG(trace, "Checking for IPv6 connectivity.");
  int domain = AF_INET6;
  const Api::SysCallSocketResult socket_result =
      Api::OsSysCallsSingleton::get().socket(domain, SOCK_DGRAM, /* protocol= */ 0);
  if (!SOCKET_VALID(socket_result.return_value_)) {
    ENVOY_LOG(trace, "Unable to create a datagram socket with errno: {}.", socket_result.errno_);
    return false;
  }
  Network::IoSocketHandleImpl socket_handle(socket_result.return_value_, /* socket_v6only= */ true,
                                            {domain});
  Api::SysCallIntResult connect_result =
      socket_handle.connect(std::make_shared<Network::Address::Ipv6Instance>(
          std::string(IPV6_PROBE_ADDRESS), IPV6_PROBE_PORT));
  bool has_ipv6_connectivity = connect_result.return_value_ == 0;
  if (has_ipv6_connectivity) {
    ENVOY_LOG(trace, "Found IPv6 connectivity.");
  } else {
    ENVOY_LOG(trace, "No IPv6 connectivity found with errno: {}.", connect_result.errno_);
  }
  return has_ipv6_connectivity;
}

} // namespace Envoy
