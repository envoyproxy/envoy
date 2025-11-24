#include "library/common/internal_engine.h"

#include <sys/resource.h>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/utility.h"
#include "source/common/http/http_server_properties_cache_manager_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "library/common/mobile_process_wide.h"
#include "library/common/network/network_types.h"
#include "library/common/network/proxy_api.h"
#include "library/common/stats/utility.h"

namespace Envoy {
namespace {
constexpr absl::Duration ENGINE_RUNNING_TIMEOUT = absl::Seconds(30);

// There is only one shared MobileProcessWide instance for all Envoy Mobile engines.
MobileProcessWide& initOnceMobileProcessWide(const OptionsImplBase& options) {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(MobileProcessWide, options);
}

Network::Address::InstanceConstSharedPtr ipv6ProbeAddr() {
  // Use Google DNS IPv6 address for IPv6 probes.
  // Same as Chromium:
  // https://source.chromium.org/chromium/chromium/src/+/main:net/dns/host_resolver_manager.cc;l=155;drc=7b232da0f22e8cdf555d43c52b6491baeb87f729.
  CONSTRUCT_ON_FIRST_USE(Network::Address::InstanceConstSharedPtr,
                         new Network::Address::Ipv6Instance("2001:4860:4860::8888", 53));
}

Network::Address::InstanceConstSharedPtr ipv4ProbeAddr() {
  // Use Google DNS IPv4 address for IPv4 probes.
  CONSTRUCT_ON_FIRST_USE(Network::Address::InstanceConstSharedPtr,
                         new Network::Address::Ipv4Instance("8.8.8.8", 53));
}

bool hasNetworkChanged(int network_type, int prev_network_type) {
  // The network types are both cellular and don't differ in the Network::Generic (e.g. VPN) flag,
  // so don't consider it a network change. If the previous and current network types differ, it's a
  // difference in cellular network subtypes (e.g. 4G to 5G).
  if ((network_type & static_cast<int>(NetworkType::WWAN)) &&
      (prev_network_type & static_cast<int>(NetworkType::WWAN)) &&
      (network_type & static_cast<int>(NetworkType::Generic)) ==
          (prev_network_type & static_cast<int>(NetworkType::Generic))) {
    return false;
  }
  // Otherwise, if the current network type is different from the previous network type, it's a
  // network change.
  return network_type != prev_network_type;
}

bool areIpAddressesDifferent(const Network::Address::InstanceConstSharedPtr& addr,
                             const Network::Address::InstanceConstSharedPtr& prev_addr) {
  if (addr == nullptr && prev_addr == nullptr) {
    return false;
  }
  if (addr == nullptr || prev_addr == nullptr) {
    // Change in presence of an IP address is treated as a difference.
    return true;
  }
  if (addr->ip()->version() != prev_addr->ip()->version()) {
    // IP address versions are different.
    return true;
  }
  if (addr->ip()->version() == Network::Address::IpVersion::v6) {
    // Both are IPv6 but the addresses are different.
    return addr->ip()->ipv6()->address() != prev_addr->ip()->ipv6()->address();
  }
  // Both are IPv4 but the addresses are different.
  return addr->ip()->ipv4()->address() != prev_addr->ip()->ipv4()->address();
}
} // namespace

static std::atomic<envoy_stream_t> current_stream_handle_{0};

InternalEngine::InternalEngine(std::unique_ptr<EngineCallbacks> callbacks,
                               std::unique_ptr<EnvoyLogger> logger,
                               std::unique_ptr<EnvoyEventTracker> event_tracker,
                               absl::optional<int> thread_priority,
                               absl::optional<size_t> high_watermark,
                               bool disable_dns_refresh_on_network_change,
                               Thread::PosixThreadFactoryPtr thread_factory, bool enable_logger)
    : thread_factory_(std::move(thread_factory)), callbacks_(std::move(callbacks)),
      logger_(std::move(logger)), event_tracker_(std::move(event_tracker)),
      thread_priority_(thread_priority), high_watermark_(high_watermark),
      dispatcher_(std::make_unique<Event::ProvisionalDispatcher>()),
      disable_dns_refresh_on_network_change_(disable_dns_refresh_on_network_change),
      enable_logger_(enable_logger) {
  ExtensionRegistry::registerFactories();

  Api::External::registerApi(std::string(ENVOY_EVENT_TRACKER_API_NAME), &event_tracker_);
}

InternalEngine::InternalEngine(std::unique_ptr<EngineCallbacks> callbacks,
                               std::unique_ptr<EnvoyLogger> logger,
                               std::unique_ptr<EnvoyEventTracker> event_tracker,
                               absl::optional<int> thread_priority,
                               absl::optional<size_t> high_watermark,
                               bool disable_dns_refresh_on_network_change, bool enable_logger)
    : InternalEngine(std::move(callbacks), std::move(logger), std::move(event_tracker),
                     thread_priority, high_watermark, disable_dns_refresh_on_network_change,
                     Thread::PosixThreadFactory::create(), enable_logger) {}

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
envoy_status_t InternalEngine::run(std::shared_ptr<OptionsImplBase> options) {
  initOnceMobileProcessWide(*options);
  Thread::Options thread_options;
  thread_options.priority_ = thread_priority_;
  main_thread_ = thread_factory_->createThread([this, options]() mutable -> void { main(options); },
                                               thread_options, /* crash_on_failure= */ false);
  return (main_thread_ != nullptr) ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

envoy_status_t InternalEngine::main(std::shared_ptr<OptionsImplBase> options) {
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
      if (enable_logger_) {
        if (logger_ != nullptr) {
          log_delegate_ptr_ = std::make_unique<Logger::LambdaDelegate>(std::move(logger_),
                                                                       Logger::Registry::getSink());
        } else {
          log_delegate_ptr_ =
              std::make_unique<Logger::DefaultDelegate>(log_mutex_, Logger::Registry::getSink());
        }
      }

      main_common = std::make_unique<EngineCommon>(options);
      server_ = main_common->server();
      event_dispatcher_ = &server_->dispatcher();

      // If proxy resolution APIs are configured on the Engine, set the main thread's dispatcher
      // on the proxy resolver for handling callbacks.
      auto* proxy_resolver = static_cast<Network::ProxyResolverApi*>(
          Api::External::retrieveApi("envoy_proxy_resolver", /*allow_absent=*/true));
      if (proxy_resolver != nullptr) {
        proxy_resolver->resolver->setDispatcher(event_dispatcher_);
      }

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
          Network::DefaultNetworkChangeCallback cb =
              [this](envoy_netconf_t current_configuration_key) {
                dispatcher_->post([this, current_configuration_key]() {
                  if (connectivity_manager_->getConfigurationKey() != current_configuration_key) {
                    // The default network has changed to a different one.
                    return;
                  }
                  ENVOY_LOG_MISC(
                      trace,
                      "Default network state has been changed. Current net configuration key {}",
                      current_configuration_key);
                  resetHttpPropertiesAndDrainHosts(probeAndGetLocalAddr(AF_INET6) != nullptr);
                  if (!disable_dns_refresh_on_network_change_) {
                    // This call will possibly drain all connections asynchronously.
                    connectivity_manager_->doRefreshDns(current_configuration_key,
                                                        /*drain_connections=*/true);
                  }
                });
              };
          connectivity_manager_->setDefaultNetworkChangeCallback(std::move(cb));
          if (Runtime::runtimeFeatureEnabled(
                  "envoy.reloadable_features.dns_cache_set_ip_version_to_remove")) {
            if (probeAndGetLocalAddr(AF_INET6) == nullptr) {
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
          http_client_ = std::make_unique<Http::Client>(
              std::move(api_listener), *dispatcher_, server_->serverFactoryContext().scope(),
              server_->api().randomGenerator(), high_watermark_);
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

envoy_status_t InternalEngine::setProxySettings(absl::string_view hostname, const uint16_t port) {
  return dispatcher_->post([&, host = std::string(hostname), port]() -> void {
    connectivity_manager_->setProxySettings(Network::ProxySettings::parseHostAndPort(host, port));
  });
}

envoy_status_t InternalEngine::resetConnectivityState() {
  return dispatcher_->post([&]() -> void { connectivity_manager_->resetConnectivityState(); });
}

void InternalEngine::onDefaultNetworkAvailable() {
  ENVOY_LOG_MISC(trace, "Calling the default network available callback");
  // TODO(abeyad): Add a call to re-add DNS records for refreshing based on their TTL.
}

void InternalEngine::onDefaultNetworkChangeEvent(const int network_type) {
  ENVOY_LOG_MISC(trace, "Calling the default network change event callback");

  dispatcher_->post([&, network_type]() -> void {
    Network::Address::InstanceConstSharedPtr local_addr = probeAndGetLocalAddr(AF_INET6);
    const bool has_ipv6_connectivity = local_addr != nullptr;
    if (local_addr == nullptr) {
      // If there's no IPv6 connectivity, get the IPv4 local address.
      local_addr = probeAndGetLocalAddr(AF_INET);
    }

    if (hasNetworkChanged(network_type, prev_network_type_) ||
        areIpAddressesDifferent(local_addr, prev_local_addr_)) {
      // If the IP addresses are not the same between network change events, or the network type
      // changed and it's not just a cellular subtype change (e.g. 4g to 5g), then assume there was
      // an actual network change.
      handleNetworkChange(network_type, has_ipv6_connectivity);
    }

    prev_network_type_ = network_type;
    prev_local_addr_ = local_addr;

    ENVOY_LOG_MISC(trace, "Finished the network changed callback");
  });
}

// TODO(abeyad): Delete this function after onDefaultNetworkChangeEvent() is tested and becomes the
// default.
void InternalEngine::onDefaultNetworkChanged(int network) {
  ENVOY_LOG_MISC(trace, "Calling the default network changed callback");
  dispatcher_->post([&, network]() -> void {
    handleNetworkChange(network, probeAndGetLocalAddr(AF_INET6) != nullptr);
  });
}

void InternalEngine::onDefaultNetworkChangedAndroid(ConnectionType connection_type,
                                                    int64_t net_id) {
  if (engine_running_.HasBeenNotified()) {
    connectivity_manager_->onDefaultNetworkChangedAndroid(connection_type, net_id);
  }
}

void InternalEngine::onNetworkDisconnectAndroid(int64_t net_id) {
  if (engine_running_.HasBeenNotified()) {
    connectivity_manager_->onNetworkDisconnectAndroid(net_id);
  }
}

void InternalEngine::onNetworkConnectAndroid(ConnectionType connection_type, int64_t net_id) {
  if (engine_running_.HasBeenNotified()) {
    connectivity_manager_->onNetworkConnectAndroid(connection_type, net_id);
  }
}

void InternalEngine::purgeActiveNetworkListAndroid(const std::vector<int64_t>& active_network_ids) {
  if (engine_running_.HasBeenNotified()) {
    connectivity_manager_->purgeActiveNetworkListAndroid(active_network_ids);
  }
}

void InternalEngine::onDefaultNetworkUnavailable() {
  ENVOY_LOG_MISC(trace, "Calling the default network unavailable callback");
  dispatcher_->post([&]() -> void { connectivity_manager_->dnsCache()->stop(); });
}

void InternalEngine::handleNetworkChange(const int network_type, const bool has_ipv6_connectivity) {
  envoy_netconf_t configuration = connectivity_manager_->setPreferredNetwork(network_type);

  resetHttpPropertiesAndDrainHosts(has_ipv6_connectivity);
  if (!disable_dns_refresh_on_network_change_) {
    // Refresh DNS upon network changes.
    // This call will possibly drain all connections asynchronously.
    connectivity_manager_->refreshDns(configuration, /*drain_connections=*/true);
  }
}

void InternalEngine::resetHttpPropertiesAndDrainHosts(bool has_ipv6_connectivity) {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dns_cache_set_ip_version_to_remove") ||
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dns_cache_filter_unusable_ip_version")) {
    // The IP version to remove flag must be set first before refreshing the DNS cache so that
    // the DNS cache will be updated with whether or not the IPv6 addresses will need to be
    // removed.
    if (!has_ipv6_connectivity) {
      connectivity_manager_->dnsCache()->setIpVersionToRemove({Network::Address::IpVersion::v6});
    } else {
      connectivity_manager_->dnsCache()->setIpVersionToRemove(absl::nullopt);
    }
  }
  Http::HttpServerPropertiesCacheManager& cache_manager =
      server_->httpServerPropertiesCacheManager();
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_no_tcp_delay")) {
    // Reset HTTP/3 status for all origins.
    Http::HttpServerPropertiesCacheManager::CacheFn reset_status =
        [](Http::HttpServerPropertiesCache& cache) { cache.resetStatus(); };
    cache_manager.forEachThreadLocalCache(reset_status);
  } else {
    // Reset HTTP/3 status only for origins marked as broken.
    Http::HttpServerPropertiesCacheManager::CacheFn clear_brokenness =
        [](Http::HttpServerPropertiesCache& cache) { cache.resetBrokenness(); };
    cache_manager.forEachThreadLocalCache(clear_brokenness);
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.decouple_explicit_drain_pools_and_dns_refresh") ||
      disable_dns_refresh_on_network_change_) {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.drain_pools_on_network_change")) {
      // Since DNS refreshing is disabled, explicitly drain all non-migratable connections.
      ENVOY_LOG_EVENT(debug, "netconf_immediate_drain", "DrainAllHosts");
      getClusterManager().drainConnections(
          [](const Upstream::Host&) { return true; },
          Envoy::ConnectionPool::DrainBehavior::DrainExistingNonMigratableConnections);
    }
  }
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

Network::Address::InstanceConstSharedPtr InternalEngine::probeAndGetLocalAddr(int domain) {
  // This probing logic is borrowed from Chromium.
  // https://source.chromium.org/chromium/chromium/src/+/main:net/dns/host_resolver_manager.cc;l=1467-1488;drc=7b232da0f22e8cdf555d43c52b6491baeb87f729
  ENVOY_LOG(trace, "Checking for {} connectivity.", domain == AF_INET6 ? "IPv6" : "IPv4");
  const Api::SysCallSocketResult socket_result =
      Api::OsSysCallsSingleton::get().socket(domain, SOCK_DGRAM, /* protocol= */ 0);
  if (!SOCKET_VALID(socket_result.return_value_)) {
    ENVOY_LOG(trace, "Unable to create a datagram socket with errno: {}.", socket_result.errno_);
    return nullptr;
  }
  Network::IoSocketHandleImpl socket_handle(socket_result.return_value_,
                                            /* socket_v6only= */ domain == AF_INET6, {domain});
  Api::SysCallIntResult connect_result =
      socket_handle.connect(domain == AF_INET6 ? ipv6ProbeAddr() : ipv4ProbeAddr());
  if (connect_result.return_value_ != 0) {
    ENVOY_LOG(trace, "No {} connectivity found with errno: {}.",
              domain == AF_INET6 ? "IPv6" : "IPv4", connect_result.errno_);
    return nullptr;
  }

  absl::StatusOr<Network::Address::InstanceConstSharedPtr> address = socket_handle.localAddress();
  if (!address.status().ok()) {
    ENVOY_LOG(trace, "Local address error: {}", address.status().message());
    return nullptr;
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.mobile_ipv6_probe_simple_filtering")) {
    if ((*address)->ip() == nullptr) {
      ENVOY_LOG(trace, "Local address is not an IP address: {}.", (*address)->asString());
      return nullptr;
    }
    if ((*address)->ip()->isLinkLocalAddress()) {
      ENVOY_LOG(trace, "Ignoring link-local address: {}.", (*address)->asString());
      return nullptr;
    }
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.mobile_ipv6_probe_advanced_filtering")) {
      if ((*address)->ip()->isUniqueLocalAddress()) {
        ENVOY_LOG(trace, "Ignoring unique-local address: {}.", (*address)->asString());
        return nullptr;
      }
      if ((*address)->ip()->isSiteLocalAddress()) {
        ENVOY_LOG(trace, "Ignoring site-local address: {}.", (*address)->asString());
        return nullptr;
      }
      if ((*address)->ip()->isTeredoAddress()) {
        ENVOY_LOG(trace, "Ignoring teredo address: {}.", (*address)->asString());
        return nullptr;
      }
    }
  }

  ENVOY_LOG(trace, "Found {} connectivity.", domain == AF_INET6 ? "IPv6" : "IPv4");
  return *address;
}

} // namespace Envoy
