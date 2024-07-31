#include "source/server/server.h"

#include <csignal>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/common/exception.h"
#include "envoy/common/time.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.validate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/api/api_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/mutex_tracer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/config/xds_resource.h"
#include "source/common/http/codes.h"
#include "source/common/http/headers.h"
#include "source/common/local_info/local_info_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/runtime/runtime_keys.h"
#include "source/common/signal/fatal_error_handler.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/upstream/cluster_manager_impl.h"
#include "source/common/version/version.h"
#include "source/server/configuration_impl.h"
#include "source/server/listener_hooks.h"
#include "source/server/listener_manager_factory.h"
#include "source/server/regex_engine.h"
#include "source/server/utils.h"

namespace Envoy {
namespace Server {

namespace {
std::unique_ptr<ConnectionHandler> getHandler(Event::Dispatcher& dispatcher) {

  auto* factory = Config::Utility::getFactoryByName<ConnectionHandlerFactory>(
      "envoy.connection_handler.default");
  if (factory) {
    return factory->createConnectionHandler(dispatcher, absl::nullopt);
  }
  ENVOY_LOG_MISC(debug, "Unable to find envoy.connection_handler.default factory");
  return nullptr;
}

} // namespace

InstanceBase::InstanceBase(Init::Manager& init_manager, const Options& options,
                           Event::TimeSystem& time_system, ListenerHooks& hooks,
                           HotRestart& restarter, Stats::StoreRoot& store,
                           Thread::BasicLockable& access_log_lock,
                           Random::RandomGeneratorPtr&& random_generator,
                           ThreadLocal::Instance& tls, Thread::ThreadFactory& thread_factory,
                           Filesystem::Instance& file_system,
                           std::unique_ptr<ProcessContext> process_context,
                           Buffer::WatermarkFactorySharedPtr watermark_factory)
    : init_manager_(init_manager), live_(false), options_(options),
      validation_context_(options_.allowUnknownStaticFields(),
                          !options.rejectUnknownDynamicFields(),
                          options.ignoreUnknownDynamicFields()),
      time_source_(time_system), restarter_(restarter), start_time_(time(nullptr)),
      original_start_time_(start_time_), stats_store_(store), thread_local_(tls),
      random_generator_(std::move(random_generator)),
      api_(new Api::Impl(
          thread_factory, store, time_system, file_system, *random_generator_, bootstrap_,
          process_context ? ProcessContextOptRef(std::ref(*process_context)) : absl::nullopt,
          watermark_factory)),
      dispatcher_(api_->allocateDispatcher("main_thread")),
      access_log_manager_(options.fileFlushIntervalMsec(), *api_, *dispatcher_, access_log_lock,
                          store),
      handler_(getHandler(*dispatcher_)), worker_factory_(thread_local_, *api_, hooks),
      mutex_tracer_(options.mutexTracingEnabled() ? &Envoy::MutexTracerImpl::getOrCreateTracer()
                                                  : nullptr),
      grpc_context_(store.symbolTable()), http_context_(store.symbolTable()),
      router_context_(store.symbolTable()), process_context_(std::move(process_context)),
      hooks_(hooks), quic_stat_names_(store.symbolTable()), server_contexts_(*this),
      enable_reuse_port_default_(true), stats_flush_in_progress_(false) {}

InstanceBase::~InstanceBase() {
  terminate();

  // Stop logging to file before all the AccessLogManager and its dependencies are
  // destructed to avoid crashing at shutdown.
  file_logger_.reset();

  // Destruct the ListenerManager explicitly, before InstanceBase's local init_manager_ is
  // destructed.
  //
  // The ListenerManager's DestinationPortsMap contains FilterChainSharedPtrs. There is a rare race
  // condition where one of these FilterChains contains an HttpConnectionManager, which contains an
  // RdsRouteConfigProvider, which contains an RdsRouteConfigSubscriptionSharedPtr. Since
  // RdsRouteConfigSubscription is an Init::Target, ~RdsRouteConfigSubscription triggers a callback
  // set at initialization, which goes to unregister it from the top-level InitManager, which has
  // already been destructed (use-after-free) causing a segfault.
  ENVOY_LOG(debug, "destroying listener manager");
  listener_manager_.reset();
  ENVOY_LOG(debug, "destroyed listener manager");
  dispatcher_->shutdown();

#ifdef ENVOY_PERFETTO
  if (tracing_session_ != nullptr) {
    // Flush the trace data.
    perfetto::TrackEvent::Flush();
    // Disable tracing and block until tracing has stopped.
    tracing_session_->StopBlocking();
    close(tracing_fd_);
  }
#endif
}

Upstream::ClusterManager& InstanceBase::clusterManager() {
  ASSERT(config_.clusterManager() != nullptr);
  return *config_.clusterManager();
}

const Upstream::ClusterManager& InstanceBase::clusterManager() const {
  ASSERT(config_.clusterManager() != nullptr);
  return *config_.clusterManager();
}

void InstanceBase::drainListeners(OptRef<const Network::ExtraShutdownListenerOptions> options) {
  ENVOY_LOG(info, "closing and draining listeners");
  listener_manager_->stopListeners(ListenerManager::StopListenersType::All,
                                   options.has_value() ? *options
                                                       : Network::ExtraShutdownListenerOptions{});
  drain_manager_->startDrainSequence([] {});
}

void InstanceBase::failHealthcheck(bool fail) {
  live_.store(!fail);
  server_stats_->live_.set(live_.load());
}

MetricSnapshotImpl::MetricSnapshotImpl(Stats::Store& store,
                                       Upstream::ClusterManager& cluster_manager,
                                       TimeSource& time_source) {
  store.forEachSinkedCounter(
      [this](std::size_t size) {
        snapped_counters_.reserve(size);
        counters_.reserve(size);
      },
      [this](Stats::Counter& counter) {
        snapped_counters_.push_back(Stats::CounterSharedPtr(&counter));
        counters_.push_back({counter.latch(), counter});
      });

  store.forEachSinkedGauge(
      [this](std::size_t size) {
        snapped_gauges_.reserve(size);
        gauges_.reserve(size);
      },
      [this](Stats::Gauge& gauge) {
        snapped_gauges_.push_back(Stats::GaugeSharedPtr(&gauge));
        gauges_.push_back(gauge);
      });

  store.forEachSinkedHistogram(
      [this](std::size_t size) {
        snapped_histograms_.reserve(size);
        histograms_.reserve(size);
      },
      [this](Stats::ParentHistogram& histogram) {
        snapped_histograms_.push_back(Stats::ParentHistogramSharedPtr(&histogram));
        histograms_.push_back(histogram);
      });

  store.forEachSinkedTextReadout(
      [this](std::size_t size) {
        snapped_text_readouts_.reserve(size);
        text_readouts_.reserve(size);
      },
      [this](Stats::TextReadout& text_readout) {
        snapped_text_readouts_.push_back(Stats::TextReadoutSharedPtr(&text_readout));
        text_readouts_.push_back(text_readout);
      });

  Upstream::HostUtility::forEachHostMetric(
      cluster_manager,
      [this](Stats::PrimitiveCounterSnapshot&& metric) {
        host_counters_.emplace_back(std::move(metric));
      },
      [this](Stats::PrimitiveGaugeSnapshot&& metric) {
        host_gauges_.emplace_back(std::move(metric));
      });

  snapshot_time_ = time_source.systemTime();
}

void InstanceUtil::flushMetricsToSinks(const std::list<Stats::SinkPtr>& sinks, Stats::Store& store,
                                       Upstream::ClusterManager& cm, TimeSource& time_source) {
  // Create a snapshot and flush to all sinks.
  // NOTE: Even if there are no sinks, creating the snapshot has the important property that it
  //       latches all counters on a periodic basis. The hot restart code assumes this is being
  //       done so this should not be removed.
  MetricSnapshotImpl snapshot(store, cm, time_source);
  for (const auto& sink : sinks) {
    sink->flush(snapshot);
  }
}

void InstanceBase::flushStats() {
  if (stats_flush_in_progress_) {
    ENVOY_LOG(debug, "skipping stats flush as flush is already in progress");
    server_stats_->dropped_stat_flushes_.inc();
    return;
  }

  stats_flush_in_progress_ = true;
  ENVOY_LOG(debug, "flushing stats");
  // If Envoy is not fully initialized, workers will not be started and mergeHistograms
  // completion callback is not called immediately. As a result of this server stats will
  // not be updated and flushed to stat sinks. So skip mergeHistograms call if workers are
  // not started yet.
  if (initManager().state() == Init::Manager::State::Initialized) {
    // A shutdown initiated before this callback may prevent this from being called as per
    // the semantics documented in ThreadLocal's runOnAllThreads method.
    stats_store_.mergeHistograms([this]() -> void { flushStatsInternal(); });
  } else {
    ENVOY_LOG(debug, "Envoy is not fully initialized, skipping histogram merge and flushing stats");
    flushStatsInternal();
  }
}

void InstanceBase::updateServerStats() {
  // mergeParentStatsIfAny() does nothing and returns a struct of 0s if there is no parent.
  HotRestart::ServerStatsFromParent parent_stats = restarter_.mergeParentStatsIfAny(stats_store_);

  server_stats_->uptime_.set(time(nullptr) - original_start_time_);
  server_stats_->memory_allocated_.set(Memory::Stats::totalCurrentlyAllocated() +
                                       parent_stats.parent_memory_allocated_);
  server_stats_->memory_heap_size_.set(Memory::Stats::totalCurrentlyReserved());
  server_stats_->memory_physical_size_.set(Memory::Stats::totalPhysicalBytes());
  if (!options().hotRestartDisabled()) {
    server_stats_->parent_connections_.set(parent_stats.parent_connections_);
  }
  server_stats_->total_connections_.set(listener_manager_->numConnections() +
                                        parent_stats.parent_connections_);
  server_stats_->days_until_first_cert_expiring_.set(
      sslContextManager().daysUntilFirstCertExpires().value_or(0));

  auto secs_until_ocsp_response_expires =
      sslContextManager().secondsUntilFirstOcspResponseExpires();
  if (secs_until_ocsp_response_expires) {
    server_stats_->seconds_until_first_ocsp_response_expiring_.set(
        secs_until_ocsp_response_expires.value());
  }
  server_stats_->state_.set(
      enumToInt(Utility::serverState(initManager().state(), healthCheckFailed())));
  server_stats_->stats_recent_lookups_.set(
      stats_store_.symbolTable().getRecentLookups([](absl::string_view, uint64_t) {}));
}

void InstanceBase::flushStatsInternal() {
  updateServerStats();
  auto& stats_config = config_.statsConfig();
  InstanceUtil::flushMetricsToSinks(stats_config.sinks(), stats_store_, clusterManager(),
                                    timeSource());
  // TODO(ramaraochavali): consider adding different flush interval for histograms.
  if (stat_flush_timer_ != nullptr) {
    stat_flush_timer_->enableTimer(stats_config.flushInterval());
  }

  stats_flush_in_progress_ = false;
}

bool InstanceBase::healthCheckFailed() { return !live_.load(); }

ProcessContextOptRef InstanceBase::processContext() {
  if (process_context_ == nullptr) {
    return absl::nullopt;
  }

  return *process_context_;
}

namespace {

bool canBeRegisteredAsInlineHeader(const Http::LowerCaseString& header_name) {
  // 'set-cookie' cannot currently be registered as an inline header.
  if (header_name == Http::Headers::get().SetCookie) {
    return false;
  }
  return true;
}

absl::Status
registerCustomInlineHeadersFromBootstrap(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  for (const auto& inline_header : bootstrap.inline_headers()) {
    const Http::LowerCaseString lower_case_name(inline_header.inline_header_name());
    if (!canBeRegisteredAsInlineHeader(lower_case_name)) {
      return absl::InvalidArgumentError(
          fmt::format("Header {} cannot be registered as an inline header.",
                      inline_header.inline_header_name()));
    }
    switch (inline_header.inline_header_type()) {
    case envoy::config::bootstrap::v3::CustomInlineHeader::REQUEST_HEADER:
      Http::CustomInlineHeaderRegistry::registerInlineHeader<
          Http::RequestHeaderMap::header_map_type>(lower_case_name);
      break;
    case envoy::config::bootstrap::v3::CustomInlineHeader::REQUEST_TRAILER:
      Http::CustomInlineHeaderRegistry::registerInlineHeader<
          Http::RequestTrailerMap::header_map_type>(lower_case_name);
      break;
    case envoy::config::bootstrap::v3::CustomInlineHeader::RESPONSE_HEADER:
      Http::CustomInlineHeaderRegistry::registerInlineHeader<
          Http::ResponseHeaderMap::header_map_type>(lower_case_name);
      break;
    case envoy::config::bootstrap::v3::CustomInlineHeader::RESPONSE_TRAILER:
      Http::CustomInlineHeaderRegistry::registerInlineHeader<
          Http::ResponseTrailerMap::header_map_type>(lower_case_name);
      break;
    default:
      PANIC("not implemented");
    }
  }
  return absl::OkStatus();
}

} // namespace

absl::Status InstanceUtil::loadBootstrapConfig(
    envoy::config::bootstrap::v3::Bootstrap& bootstrap, const Options& options,
    ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api) {
  const std::string& config_path = options.configPath();
  const std::string& config_yaml = options.configYaml();
  const envoy::config::bootstrap::v3::Bootstrap& config_proto = options.configProto();

  // One of config_path and config_yaml or bootstrap should be specified.
  if (config_path.empty() && config_yaml.empty() && config_proto.ByteSizeLong() == 0) {
    return absl::InvalidArgumentError(
        "At least one of --config-path or --config-yaml or Options::configProto() "
        "should be non-empty");
  }

  if (!config_path.empty()) {
    MessageUtil::loadFromFile(config_path, bootstrap, validation_visitor, api);
  }
  if (!config_yaml.empty()) {
    envoy::config::bootstrap::v3::Bootstrap bootstrap_override;
#ifdef ENVOY_ENABLE_YAML
    MessageUtil::loadFromYaml(config_yaml, bootstrap_override, validation_visitor);
    // TODO(snowp): The fact that we do a merge here doesn't seem to be covered under test.
#else
    // Treat the yaml as proto
    Protobuf::TextFormat::ParseFromString(config_yaml, &bootstrap_override);
#endif
    bootstrap.MergeFrom(bootstrap_override);
  }
  if (config_proto.ByteSizeLong() != 0) {
    bootstrap.MergeFrom(config_proto);
  }
  MessageUtil::validate(bootstrap, validation_visitor);
  return absl::OkStatus();
}

void InstanceBase::initialize(Network::Address::InstanceConstSharedPtr local_address,
                              ComponentFactory& component_factory) {
  std::function set_up_logger = [&] {
    TRY_ASSERT_MAIN_THREAD {
      file_logger_ = std::make_unique<Logger::FileSinkDelegate>(
          options_.logPath(), access_log_manager_, Logger::Registry::getSink());
    }
    END_TRY
    CATCH(const EnvoyException& e, {
      throw EnvoyException(
          fmt::format("Failed to open log-file '{}'. e.what(): {}", options_.logPath(), e.what()));
    });
  };

  TRY_ASSERT_MAIN_THREAD {
    if (!options_.logPath().empty()) {
      set_up_logger();
    }
    restarter_.initialize(*dispatcher_, *this);
    drain_manager_ = component_factory.createDrainManager(*this);
    THROW_IF_NOT_OK(initializeOrThrow(std::move(local_address), component_factory));
  }
  END_TRY
  MULTI_CATCH(
      const EnvoyException& e,
      {
        ENVOY_LOG(critical, "error initializing config '{} {} {}': {}",
                  options_.configProto().DebugString(), options_.configYaml(),
                  options_.configPath(), e.what());
        terminate();
        throw;
      },
      {
        ENVOY_LOG(critical, "error initializing due to unknown exception");
        terminate();
        throw;
      });
}

absl::Status InstanceBase::initializeOrThrow(Network::Address::InstanceConstSharedPtr local_address,
                                             ComponentFactory& component_factory) {
  ENVOY_LOG(info, "initializing epoch {} (base id={}, hot restart version={})",
            options_.restartEpoch(), restarter_.baseId(), restarter_.version());

  ENVOY_LOG(info, "statically linked extensions:");
  for (const auto& ext : Envoy::Registry::FactoryCategoryRegistry::registeredFactories()) {
    ENVOY_LOG(info, "  {}: {}", ext.first, absl::StrJoin(ext.second->registeredNames(), ", "));
  }

  // The main thread is registered for thread local updates so that code that does not care
  // whether it runs on the main thread or on workers can still use TLS.
  // We do this as early as possible because this has no side effect and could ensure that the
  // TLS always contains a valid main thread dispatcher when TLS is used.
  thread_local_.registerThread(*dispatcher_, true);

  // Handle configuration that needs to take place prior to the main configuration load.
  RETURN_IF_NOT_OK(InstanceUtil::loadBootstrapConfig(
      bootstrap_, options_, messageValidationContext().staticValidationVisitor(), *api_));
  bootstrap_config_update_time_ = time_source_.systemTime();

  if (bootstrap_.has_application_log_config()) {
    RETURN_IF_NOT_OK(
        Utility::assertExclusiveLogFormatMethod(options_, bootstrap_.application_log_config()));
    RETURN_IF_NOT_OK(Utility::maybeSetApplicationLogFormat(bootstrap_.application_log_config()));
  }

#ifdef ENVOY_PERFETTO
  perfetto::TracingInitArgs args;
  // Include in-process events only.
  args.backends = perfetto::kInProcessBackend;
  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  // Prepare a configuration for a new "Perfetto" tracing session.
  perfetto::TraceConfig cfg;
  // TODO(rojkov): make the tracer configurable with either "Perfetto"'s native
  // message or custom one embedded into Bootstrap.
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  const std::string pftrace_path =
      PROTOBUF_GET_STRING_OR_DEFAULT(bootstrap_, perf_tracing_file_path, "envoy.pftrace");
  // Instantiate a new tracing session.
  tracing_session_ = perfetto::Tracing::NewTrace();
  tracing_fd_ = open(pftrace_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (tracing_fd_ == -1) {
    return absl::InvalidArgumentError(
        fmt::format("unable to open tracing file {}: {}", pftrace_path, errorDetails(errno)));
  }
  // Configure the tracing session.
  tracing_session_->Setup(cfg, tracing_fd_);
  // Enable tracing and block until tracing has started.
  tracing_session_->StartBlocking();
#endif

  // Immediate after the bootstrap has been loaded, override the header prefix, if configured to
  // do so. This must be set before any other code block references the HeaderValues ConstSingleton.
  if (!bootstrap_.header_prefix().empty()) {
    // setPrefix has a release assert verifying that setPrefix() is not called after prefix()
    ThreadSafeSingleton<Http::PrefixValue>::get().setPrefix(bootstrap_.header_prefix().c_str());
  }

  // Register Custom O(1) headers from bootstrap.
  RETURN_IF_NOT_OK(registerCustomInlineHeadersFromBootstrap(bootstrap_));

  ENVOY_LOG(info, "HTTP header map info:");
  for (const auto& info : Http::HeaderMapImplUtility::getAllHeaderMapImplInfo()) {
    ENVOY_LOG(info, "  {}: {} bytes: {}", info.name_, info.size_,
              absl::StrJoin(info.registered_headers_, ","));
  }

  // Initialize the regex engine and inject to singleton.
  // Needs to happen before stats store initialization because the stats
  // matcher config can include regexes.
  regex_engine_ = createRegexEngine(
      bootstrap_, messageValidationContext().staticValidationVisitor(), serverFactoryContext());

  // Needs to happen as early as possible in the instantiation to preempt the objects that require
  // stats.
  auto producer_or_error =
      Stats::TagProducerImpl::createTagProducer(bootstrap_.stats_config(), options_.statsTags());
  RETURN_IF_STATUS_NOT_OK(producer_or_error);
  stats_store_.setTagProducer(std::move(producer_or_error.value()));
  stats_store_.setStatsMatcher(std::make_unique<Stats::StatsMatcherImpl>(
      bootstrap_.stats_config(), stats_store_.symbolTable(), server_contexts_));
  stats_store_.setHistogramSettings(
      std::make_unique<Stats::HistogramSettingsImpl>(bootstrap_.stats_config(), server_contexts_));

  const std::string server_stats_prefix = "server.";
  const std::string server_compilation_settings_stats_prefix = "server.compilation_settings";
  server_stats_ = std::make_unique<ServerStats>(
      ServerStats{ALL_SERVER_STATS(POOL_COUNTER_PREFIX(stats_store_, server_stats_prefix),
                                   POOL_GAUGE_PREFIX(stats_store_, server_stats_prefix),
                                   POOL_HISTOGRAM_PREFIX(stats_store_, server_stats_prefix))});
  server_compilation_settings_stats_ =
      std::make_unique<CompilationSettings::ServerCompilationSettingsStats>(
          CompilationSettings::ServerCompilationSettingsStats{ALL_SERVER_COMPILATION_SETTINGS_STATS(
              POOL_COUNTER_PREFIX(stats_store_, server_compilation_settings_stats_prefix),
              POOL_GAUGE_PREFIX(stats_store_, server_compilation_settings_stats_prefix),
              POOL_HISTOGRAM_PREFIX(stats_store_, server_compilation_settings_stats_prefix))});
  validation_context_.setCounters(server_stats_->static_unknown_fields_,
                                  server_stats_->dynamic_unknown_fields_,
                                  server_stats_->wip_protos_);

  memory_allocator_manager_ = std::make_unique<Memory::AllocatorManager>(
      *api_, *stats_store_.rootScope(), bootstrap_.memory_allocator_manager());

  initialization_timer_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      server_stats_->initialization_time_ms_, timeSource());
  server_stats_->concurrency_.set(options_.concurrency());
  if (!options().hotRestartDisabled()) {
    server_stats_->hot_restart_epoch_.set(options_.restartEpoch());
  }
  InstanceBase::failHealthcheck(false);

  // Check if bootstrap has server version override set, if yes, we should use that as
  // 'server.version' stat.
  uint64_t version_int;
  if (bootstrap_.stats_server_version_override().value() > 0) {
    version_int = bootstrap_.stats_server_version_override().value();
  } else {
    if (!StringUtil::atoull(VersionInfo::revision().substr(0, 6).c_str(), version_int, 16)) {
      return absl::InvalidArgumentError("compiled GIT SHA is invalid. Invalid build.");
    }
  }
  server_stats_->version_.set(version_int);
  if (VersionInfo::sslFipsCompliant()) {
    server_compilation_settings_stats_->fips_mode_.set(1);
  } else {
    // Set this explicitly so that "used" flag is set so that it can be pushed to stats sinks.
    server_compilation_settings_stats_->fips_mode_.set(0);
  }

  // If user has set user_agent_name in the bootstrap config, use it.
  // Default to "envoy" if unset.
  if (bootstrap_.node().user_agent_name().empty()) {
    bootstrap_.mutable_node()->set_user_agent_name("envoy");
  }

  // If user has set user_agent_build_version in the bootstrap config, use it.
  // Default to the internal server version.
  if (!bootstrap_.node().user_agent_build_version().has_version()) {
    *bootstrap_.mutable_node()->mutable_user_agent_build_version() = VersionInfo::buildVersion();
  }

  for (const auto& ext : Envoy::Registry::FactoryCategoryRegistry::registeredFactories()) {
    auto registered_types = ext.second->registeredTypes();
    for (const auto& name : ext.second->allRegisteredNames()) {
      auto* extension = bootstrap_.mutable_node()->add_extensions();
      extension->set_name(std::string(name));
      extension->set_category(ext.first);
      auto const version = ext.second->getFactoryVersion(name);
      if (version) {
        *extension->mutable_version() = version.value();
      }
      extension->set_disabled(ext.second->isFactoryDisabled(name));
      auto it = registered_types.find(name);
      if (it != registered_types.end()) {
        std::sort(it->second.begin(), it->second.end());
        for (const auto& type_url : it->second) {
          extension->add_type_urls(type_url);
        }
      }
    }
  }

  local_info_ = std::make_unique<LocalInfo::LocalInfoImpl>(
      stats().symbolTable(), bootstrap_.node(), bootstrap_.node_context_params(), local_address,
      options_.serviceZone(), options_.serviceClusterName(), options_.serviceNodeName());

  absl::Status creation_status;
  Configuration::InitialImpl initial_config(bootstrap_, creation_status);
  RETURN_IF_NOT_OK(creation_status);

  // Learn original_start_time_ if our parent is still around to inform us of it.
  const auto parent_admin_shutdown_response = restarter_.sendParentAdminShutdownRequest();
  if (parent_admin_shutdown_response.has_value()) {
    original_start_time_ = parent_admin_shutdown_response.value().original_start_time_;
    // TODO(soulxu): This is added for switching the reuse port default value as true (#17259).
    // It ensures the same default value during the hot restart. This can be removed when
    // everyone switches to the new default value.
    enable_reuse_port_default_ =
        parent_admin_shutdown_response.value().enable_reuse_port_default_ ? true : false;
  }

  OptRef<Server::ConfigTracker> config_tracker;
#ifdef ENVOY_ADMIN_FUNCTIONALITY
  admin_ = std::make_shared<AdminImpl>(initial_config.admin().profilePath(), *this,
                                       initial_config.admin().ignoreGlobalConnLimit());

  config_tracker = admin_->getConfigTracker();
#endif
  secret_manager_ = std::make_unique<Secret::SecretManagerImpl>(config_tracker);

  loadServerFlags(initial_config.flagsPath());

  // Initialize the overload manager early so other modules can register for actions.
  auto overload_manager_or_error = createOverloadManager();
  RETURN_IF_NOT_OK(overload_manager_or_error.status());
  overload_manager_ = std::move(*overload_manager_or_error);
  null_overload_manager_ = createNullOverloadManager();

  maybeCreateHeapShrinker();

  for (const auto& bootstrap_extension : bootstrap_.bootstrap_extensions()) {
    auto& factory = Config::Utility::getAndCheckFactory<Configuration::BootstrapExtensionFactory>(
        bootstrap_extension);
    auto config = Config::Utility::translateAnyToFactoryConfig(
        bootstrap_extension.typed_config(), messageValidationContext().staticValidationVisitor(),
        factory);
    bootstrap_extensions_.push_back(
        factory.createBootstrapExtension(*config, serverFactoryContext()));
  }

  // Register the fatal actions.
  {
    FatalAction::FatalActionPtrList safe_actions;
    FatalAction::FatalActionPtrList unsafe_actions;
    for (const auto& action_config : bootstrap_.fatal_actions()) {
      auto& factory =
          Config::Utility::getAndCheckFactory<Server::Configuration::FatalActionFactory>(
              action_config.config());
      auto action = factory.createFatalActionFromProto(action_config, this);

      if (action->isAsyncSignalSafe()) {
        safe_actions.push_back(std::move(action));
      } else {
        unsafe_actions.push_back(std::move(action));
      }
    }
    Envoy::FatalErrorHandler::registerFatalActions(
        std::move(safe_actions), std::move(unsafe_actions), api_->threadFactory());
  }

  if (!bootstrap_.default_socket_interface().empty()) {
    auto& sock_name = bootstrap_.default_socket_interface();
    auto sock = const_cast<Network::SocketInterface*>(Network::socketInterface(sock_name));
    if (sock != nullptr) {
      Network::SocketInterfaceSingleton::clear();
      Network::SocketInterfaceSingleton::initialize(sock);
    }
  }

  ListenerManagerFactory* listener_manager_factory = nullptr;
  if (bootstrap_.has_listener_manager()) {
    listener_manager_factory = Config::Utility::getAndCheckFactory<ListenerManagerFactory>(
        bootstrap_.listener_manager(), false);
  } else {
    listener_manager_factory = &Config::Utility::getAndCheckFactoryByName<ListenerManagerFactory>(
        Config::ServerExtensionValues::get().DEFAULT_LISTENER);
  }

  // Workers get created first so they register for thread local updates.
  listener_manager_ = listener_manager_factory->createListenerManager(
      *this, nullptr, worker_factory_, bootstrap_.enable_dispatcher_stats(), quic_stat_names_);

  // We can now initialize stats for threading.
  stats_store_.initializeThreading(*dispatcher_, thread_local_);

  // It's now safe to start writing stats from the main thread's dispatcher.
  if (bootstrap_.enable_dispatcher_stats()) {
    dispatcher_->initializeStats(*stats_store_.rootScope(), "server.");
  }

  // The broad order of initialization from this point on is the following:
  // 1. Statically provisioned configuration (bootstrap) are loaded.
  // 2. Cluster manager is created and all primary clusters (i.e. with endpoint assignments
  //    provisioned statically in bootstrap, discovered through DNS or file based CDS) are
  //    initialized.
  // 3. Various services are initialized and configured using the bootstrap config.
  // 4. RTDS is initialized using primary clusters. This  allows runtime overrides to be fully
  //    configured before the rest of xDS configuration is provisioned.
  // 5. Secondary clusters (with endpoint assignments provisioned by xDS servers) are initialized.
  // 6. The rest of the dynamic configuration is provisioned.
  //
  // Please note: this order requires that RTDS is provisioned using a primary cluster. If RTDS is
  // provisioned through ADS then ADS must use primary cluster as well. This invariant is enforced
  // during RTDS initialization and invalid configuration will be rejected.

  // Runtime gets initialized before the main configuration since during main configuration
  // load things may grab a reference to the loader for later use.
  runtime_ = component_factory.createRuntime(*this, initial_config);
  validation_context_.setRuntime(runtime());

  if (!runtime().snapshot().getBoolean("envoy.disallow_global_stats", false)) {
    assert_action_registration_ = Assert::addDebugAssertionFailureRecordAction(
        [this](const char*) { server_stats_->debug_assertion_failures_.inc(); });
    envoy_bug_action_registration_ = Assert::addEnvoyBugFailureRecordAction(
        [this](const char*) { server_stats_->envoy_bug_failures_.inc(); });
  }

  if (initial_config.admin().address()) {
#ifdef ENVOY_ADMIN_FUNCTIONALITY
    // Admin instance always be created if admin support is not compiled out.
    RELEASE_ASSERT(admin_ != nullptr, "Admin instance should be created but actually not.");
    auto typed_admin = dynamic_cast<AdminImpl*>(admin_.get());
    RELEASE_ASSERT(typed_admin != nullptr, "Admin implementation is not an AdminImpl.");
    initial_config.initAdminAccessLog(bootstrap_, typed_admin->factoryContext());
    admin_->startHttpListener(initial_config.admin().accessLogs(), initial_config.admin().address(),
                              initial_config.admin().socketOptions());
#else
    return absl::InvalidArgumentError("Admin address configured but admin support compiled out");
#endif
  } else {
    ENVOY_LOG(info, "No admin address given, so no admin HTTP server started.");
  }
  if (admin_) {
    config_tracker_entry_ = admin_->getConfigTracker().add(
        "bootstrap", [this](const Matchers::StringMatcher&) { return dumpBootstrapConfig(); });
  }
  if (initial_config.admin().address()) {
    admin_->addListenerToHandler(handler_.get());
  }

  // Once we have runtime we can initialize the SSL context manager.
  ssl_context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(server_contexts_);

  cluster_manager_factory_ = std::make_unique<Upstream::ProdClusterManagerFactory>(
      serverFactoryContext(), stats_store_, thread_local_, http_context_,
      [this]() -> Network::DnsResolverSharedPtr { return this->getOrCreateDnsResolver(); },
      *ssl_context_manager_, *secret_manager_, quic_stat_names_, *this);

  // Now the configuration gets parsed. The configuration may start setting
  // thread local data per above. See MainImpl::initialize() for why ConfigImpl
  // is constructed as part of the InstanceBase and then populated once
  // cluster_manager_factory_ is available.
  RETURN_IF_NOT_OK(config_.initialize(bootstrap_, *this, *cluster_manager_factory_));

  // Instruct the listener manager to create the LDS provider if needed. This must be done later
  // because various items do not yet exist when the listener manager is created.
  if (bootstrap_.dynamic_resources().has_lds_config() ||
      !bootstrap_.dynamic_resources().lds_resources_locator().empty()) {
    std::unique_ptr<xds::core::v3::ResourceLocator> lds_resources_locator;
    if (!bootstrap_.dynamic_resources().lds_resources_locator().empty()) {
      lds_resources_locator = std::make_unique<xds::core::v3::ResourceLocator>(
          THROW_OR_RETURN_VALUE(Config::XdsResourceIdentifier::decodeUrl(
                                    bootstrap_.dynamic_resources().lds_resources_locator()),
                                xds::core::v3::ResourceLocator));
    }
    listener_manager_->createLdsApi(bootstrap_.dynamic_resources().lds_config(),
                                    lds_resources_locator.get());
  }

  // We have to defer RTDS initialization until after the cluster manager is
  // instantiated (which in turn relies on runtime...).
  RETURN_IF_NOT_OK(runtime().initialize(clusterManager()));

  clusterManager().setPrimaryClustersInitializedCb(
      [this]() { onClusterManagerPrimaryInitializationComplete(); });

  auto& stats_config = config_.statsConfig();
  for (const Stats::SinkPtr& sink : stats_config.sinks()) {
    stats_store_.addSink(*sink);
  }
  if (!stats_config.flushOnAdmin()) {
    // Some of the stat sinks may need dispatcher support so don't flush until the main loop starts.
    // Just setup the timer.
    stat_flush_timer_ = dispatcher_->createTimer([this]() -> void { flushStats(); });
    stat_flush_timer_->enableTimer(stats_config.flushInterval());
  }

  // Now that we are initialized, notify the bootstrap extensions.
  for (auto&& bootstrap_extension : bootstrap_extensions_) {
    bootstrap_extension->onServerInitialized();
  }

  // GuardDog (deadlock detection) object and thread setup before workers are
  // started and before our own run() loop runs.
  main_thread_guard_dog_ = maybeCreateGuardDog("main_thread");
  worker_guard_dog_ = maybeCreateGuardDog("workers");
  return absl::OkStatus();
}

void InstanceBase::onClusterManagerPrimaryInitializationComplete() {
  // If RTDS was not configured the `onRuntimeReady` callback is immediately invoked.
  runtime().startRtdsSubscriptions([this]() { onRuntimeReady(); });
}

void InstanceBase::onRuntimeReady() {
  // Begin initializing secondary clusters after RTDS configuration has been applied.
  // Initializing can throw exceptions, so catch these.
  TRY_ASSERT_MAIN_THREAD {
    THROW_IF_NOT_OK(clusterManager().initializeSecondaryClusters(bootstrap_));
  }
  END_TRY
  CATCH(const EnvoyException& e, {
    ENVOY_LOG(warn, "Skipping initialization of secondary cluster: {}", e.what());
    shutdown();
  });

  if (bootstrap_.has_hds_config()) {
    const auto& hds_config = bootstrap_.hds_config();
    async_client_manager_ = std::make_unique<Grpc::AsyncClientManagerImpl>(
        *config_.clusterManager(), thread_local_, server_contexts_, grpc_context_.statNames(),
        bootstrap_.grpc_async_client_manager_config());
    TRY_ASSERT_MAIN_THREAD {
      THROW_IF_NOT_OK(Config::Utility::checkTransportVersion(hds_config));
      // HDS does not support xDS-Failover.
      auto factory_or_error = Config::Utility::factoryForGrpcApiConfigSource(
          *async_client_manager_, hds_config, *stats_store_.rootScope(), false, 0);
      THROW_IF_STATUS_NOT_OK(factory_or_error, throw);
      hds_delegate_ = std::make_unique<Upstream::HdsDelegate>(
          serverFactoryContext(), *stats_store_.rootScope(),
          factory_or_error.value()->createUncachedRawAsyncClient(), stats_store_,
          *ssl_context_manager_, info_factory_);
    }
    END_TRY
    CATCH(const EnvoyException& e, {
      ENVOY_LOG(warn, "Skipping initialization of HDS cluster: {}", e.what());
      shutdown();
    });
  }

  // TODO (nezdolik): Fully deprecate this runtime key in the next release.
  if (runtime().snapshot().get(Runtime::Keys::GlobalMaxCxRuntimeKey)) {
    ENVOY_LOG(warn,
              "Usage of the deprecated runtime key {}, consider switching to "
              "`envoy.resource_monitors.global_downstream_max_connections` instead."
              "This runtime key will be removed in future.",
              Runtime::Keys::GlobalMaxCxRuntimeKey);
  }
}

void InstanceBase::startWorkers() {
  // The callback will be called after workers are started.
  listener_manager_->startWorkers(makeOptRefFromPtr(worker_guard_dog_.get()), [this]() {
    if (isShutdown()) {
      return;
    }

    initialization_timer_->complete();
    // Update server stats as soon as initialization is done.
    updateServerStats();
    workers_started_ = true;
    hooks_.onWorkersStarted();
    // At this point we are ready to take traffic and all listening ports are up. Notify our
    // parent if applicable that they can stop listening and drain.
    restarter_.drainParentListeners();
    drain_manager_->startParentShutdownSequence();
  });
}

Runtime::LoaderPtr InstanceUtil::createRuntime(Instance& server,
                                               Server::Configuration::Initial& config) {
#ifdef ENVOY_ENABLE_YAML
  ENVOY_LOG(info, "runtime: {}", MessageUtil::getYamlStringFromMessage(config.runtime()));
#endif
  absl::StatusOr<std::unique_ptr<Runtime::LoaderImpl>> loader = Runtime::LoaderImpl::create(
      server.dispatcher(), server.threadLocal(), config.runtime(), server.localInfo(),
      server.stats(), server.api().randomGenerator(),
      server.messageValidationContext().dynamicValidationVisitor(), server.api());
  THROW_IF_NOT_OK(loader.status());
  return std::move(loader.value());
}

void InstanceBase::loadServerFlags(const absl::optional<std::string>& flags_path) {
  if (!flags_path) {
    return;
  }

  ENVOY_LOG(info, "server flags path: {}", flags_path.value());
  if (api_->fileSystem().fileExists(flags_path.value() + "/drain")) {
    ENVOY_LOG(info, "starting server in drain mode");
    InstanceBase::failHealthcheck(true);
  }
}

RunHelper::RunHelper(Instance& instance, const Options& options, Event::Dispatcher& dispatcher,
                     Upstream::ClusterManager& cm, AccessLog::AccessLogManager& access_log_manager,
                     Init::Manager& init_manager, OverloadManager& overload_manager,
                     OverloadManager& null_overload_manager, std::function<void()> post_init_cb)
    : init_watcher_("RunHelper", [&instance, post_init_cb]() {
        if (!instance.isShutdown()) {
          post_init_cb();
        }
      }) {
  // Setup signals.
  // Since signals are not supported on Windows we have an internal definition for `SIGTERM`
  // On POSIX it resolves as expected to SIGTERM
  // On Windows we use it internally for all the console events that indicate that we should
  // terminate the process.
  if (options.signalHandlingEnabled()) {
    sigterm_ = dispatcher.listenForSignal(ENVOY_SIGTERM, [&instance]() {
      ENVOY_LOG(warn, "caught ENVOY_SIGTERM");
      instance.shutdown();
    });
#ifndef WIN32
    sigint_ = dispatcher.listenForSignal(SIGINT, [&instance]() {
      ENVOY_LOG(warn, "caught SIGINT");
      instance.shutdown();
    });

    sig_usr_1_ = dispatcher.listenForSignal(SIGUSR1, [&access_log_manager]() {
      ENVOY_LOG(info, "caught SIGUSR1. Reopening access logs.");
      access_log_manager.reopen();
    });

    sig_hup_ = dispatcher.listenForSignal(SIGHUP, []() {
      ENVOY_LOG(warn, "caught and eating SIGHUP. See documentation for how to hot restart.");
    });
#endif
  }

  // Start overload manager before workers.
  overload_manager.start();
  null_overload_manager.start();

  // If there is no global limit to the number of active connections, warn on startup.
  if (!overload_manager.getThreadLocalOverloadState().isResourceMonitorEnabled(
          Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections)) {
    ENVOY_LOG(
        warn,
        "There is no configured limit to the number of allowed active downstream "
        "connections. Configure a "
        "limit in `envoy.resource_monitors.global_downstream_max_connections` resource monitor.");
  }

  // Register for cluster manager init notification. We don't start serving worker traffic until
  // upstream clusters are initialized which may involve running the event loop. Note however that
  // this can fire immediately if all clusters have already initialized. Also note that we need
  // to guard against shutdown at two different levels since SIGTERM can come in once the run loop
  // starts.
  cm.setInitializedCb([&instance, &init_manager, &cm, this]() {
    if (instance.isShutdown()) {
      return;
    }

    const auto type_url = Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>();
    // Pause RDS to ensure that we don't send any requests until we've
    // subscribed to all the RDS resources. The subscriptions happen in the init callbacks,
    // so we pause RDS until we've completed all the callbacks.
    Config::ScopedResume maybe_resume_rds;
    if (cm.adsMux()) {
      maybe_resume_rds = cm.adsMux()->pause(type_url);
    }

    ENVOY_LOG(info, "all clusters initialized. initializing init manager");
    init_manager.initialize(init_watcher_);

    // Now that we're execute all the init callbacks we can resume RDS
    // as we've subscribed to all the statically defined RDS resources.
    // This is done by tearing down the maybe_resume_rds Cleanup object.
  });
}

void InstanceBase::run() {
  // RunHelper exists primarily to facilitate testing of how we respond to early shutdown during
  // startup (see RunHelperTest in server_test.cc).
  const auto run_helper =
      RunHelper(*this, options_, *dispatcher_, clusterManager(), access_log_manager_, init_manager_,
                overloadManager(), nullOverloadManager(), [this] {
                  notifyCallbacksForStage(Stage::PostInit);
                  startWorkers();
                });

  // Run the main dispatch loop waiting to exit.
  ENVOY_LOG(info, "starting main dispatch loop");
  WatchDogSharedPtr watchdog;
  if (main_thread_guard_dog_) {
    watchdog = main_thread_guard_dog_->createWatchDog(api_->threadFactory().currentThreadId(),
                                                      "main_thread", *dispatcher_);
  }
  dispatcher_->post([this] { notifyCallbacksForStage(Stage::Startup); });
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ENVOY_LOG(info, "main dispatch loop exited");
  if (main_thread_guard_dog_) {
    main_thread_guard_dog_->stopWatching(watchdog);
  }
  watchdog.reset();

  terminate();
}

void InstanceBase::terminate() {
  if (terminated_) {
    return;
  }
  terminated_ = true;

  // Before starting to shutdown anything else, stop slot destruction updates.
  thread_local_.shutdownGlobalThreading();

  // Before the workers start exiting we should disable stat threading.
  stats_store_.shutdownThreading();

  // TODO: figure out the correct fix: https://github.com/envoyproxy/envoy/issues/15072.
  std::vector<std::string> muxes = {
      "envoy.config_mux.new_grpc_mux_factory", "envoy.config_mux.grpc_mux_factory",
      "envoy.config_mux.delta_grpc_mux_factory", "envoy.config_mux.sotw_grpc_mux_factory"};
  for (const auto& name : muxes) {
    auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(name);
    if (factory) {
      factory->shutdownAll();
    }
  }

  if (overload_manager_) {
    overload_manager_->stop();
  }

  // Shutdown all the workers now that the main dispatch loop is done.
  if (listener_manager_ != nullptr) {
    listener_manager_->stopWorkers();
  }

  // Only flush if we have not been hot restarted.
  if (stat_flush_timer_) {
    flushStats();
  }

  if (config_.clusterManager() != nullptr) {
    config_.clusterManager()->shutdown();
  }
  handler_.reset();
  thread_local_.shutdownThread();
  restarter_.shutdown();
  ENVOY_LOG(info, "exiting");
  ENVOY_FLUSH_LOG();
  FatalErrorHandler::clearFatalActionsOnTerminate();
}

Runtime::Loader& InstanceBase::runtime() { return *runtime_; }

void InstanceBase::shutdown() {
  ENVOY_LOG(info, "shutting down server instance");
  shutdown_ = true;
  restarter_.sendParentTerminateRequest();
  notifyCallbacksForStage(Stage::ShutdownExit, [this] { dispatcher_->exit(); });
}

void InstanceBase::shutdownAdmin() {
  ENVOY_LOG(warn, "shutting down admin due to child startup");
  stat_flush_timer_.reset();
  handler_->stopListeners();
  if (admin_) {
    admin_->closeSocket();
  }

  // If we still have a parent, it should be terminated now that we have a child.
  ENVOY_LOG(warn, "terminating parent process");
  restarter_.sendParentTerminateRequest();
}

ServerLifecycleNotifier::HandlePtr InstanceBase::registerCallback(Stage stage,
                                                                  StageCallback callback) {
  auto& callbacks = stage_callbacks_[stage];
  return std::make_unique<LifecycleCallbackHandle<StageCallback>>(callbacks, callback);
}

ServerLifecycleNotifier::HandlePtr
InstanceBase::registerCallback(Stage stage, StageCallbackWithCompletion callback) {
  ASSERT(stage == Stage::ShutdownExit);
  auto& callbacks = stage_completable_callbacks_[stage];
  return std::make_unique<LifecycleCallbackHandle<StageCallbackWithCompletion>>(callbacks,
                                                                                callback);
}

void InstanceBase::notifyCallbacksForStage(Stage stage, std::function<void()> completion_cb) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  const auto stage_it = stage_callbacks_.find(stage);
  if (stage_it != stage_callbacks_.end()) {
    LifecycleNotifierCallbacks& callbacks = stage_it->second;
    for (auto callback_it = callbacks.begin(); callback_it != callbacks.end();) {
      StageCallback callback = *callback_it;
      // Increment the iterator before invoking the callback in case the
      // callback deletes the handle which will unregister itself and
      // invalidate this iterator if we're still pointing at it.
      ++callback_it;
      callback();
    }
  }

  // Wrap completion_cb so that it only gets invoked when all callbacks for this stage
  // have finished their work.
  std::shared_ptr<void> cb_guard(
      new Cleanup([this, completion_cb]() { dispatcher_->post(completion_cb); }));

  // Registrations which take a completion callback are typically implemented by executing a
  // callback on all worker threads using Slot::runOnAllThreads which will hang indefinitely if
  // worker threads have not been started so we need to skip notifications if envoy is shutdown
  // early before workers have started.
  if (workers_started_) {
    const auto it2 = stage_completable_callbacks_.find(stage);
    if (it2 != stage_completable_callbacks_.end()) {
      ENVOY_LOG(info, "Notifying {} callback(s) with completion.", it2->second.size());
      for (const StageCallbackWithCompletion& callback : it2->second) {
        callback([cb_guard] {});
      }
    }
  }
}

ProtobufTypes::MessagePtr InstanceBase::dumpBootstrapConfig() {
  auto config_dump = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
  config_dump->mutable_bootstrap()->MergeFrom(bootstrap_);
  TimestampUtil::systemClockToTimestamp(bootstrap_config_update_time_,
                                        *(config_dump->mutable_last_updated()));
  return config_dump;
}

Network::DnsResolverSharedPtr InstanceBase::getOrCreateDnsResolver() {
  if (!dns_resolver_) {
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    Network::DnsResolverFactory& dns_resolver_factory =
        Network::createDnsResolverFactoryFromProto(bootstrap_, typed_dns_resolver_config);
    dns_resolver_ = THROW_OR_RETURN_VALUE(
        dns_resolver_factory.createDnsResolver(dispatcher(), api(), typed_dns_resolver_config),
        Network::DnsResolverSharedPtr);
  }
  return dns_resolver_;
}

bool InstanceBase::enableReusePortDefault() { return enable_reuse_port_default_; }

} // namespace Server
} // namespace Envoy
