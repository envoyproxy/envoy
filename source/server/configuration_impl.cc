#include "source/server/configuration_impl.h"

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/network/connection.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"
#include "envoy/server/tracer_config.h"
#include "envoy/ssl/context_manager.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/config/runtime_utility.h"
#include "source/common/config/utility.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

bool FilterChainUtility::buildFilterChain(Network::FilterManager& filter_manager,
                                          const std::vector<Network::FilterFactoryCb>& factories) {
  for (const Network::FilterFactoryCb& factory : factories) {
    factory(filter_manager);
  }

  return filter_manager.initializeReadFilters();
}

bool FilterChainUtility::buildFilterChain(
    Network::ListenerFilterManager& filter_manager,
    const std::vector<Network::ListenerFilterFactoryCb>& factories) {
  for (const Network::ListenerFilterFactoryCb& factory : factories) {
    factory(filter_manager);
  }

  return true;
}

void FilterChainUtility::buildUdpFilterChain(
    Network::UdpListenerFilterManager& filter_manager, Network::UdpReadFilterCallbacks& callbacks,
    const std::vector<Network::UdpListenerFilterFactoryCb>& factories) {
  for (const Network::UdpListenerFilterFactoryCb& factory : factories) {
    factory(filter_manager, callbacks);
  }
}

StatsConfigImpl::StatsConfigImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  if (bootstrap.has_stats_flush_interval() &&
      bootstrap.stats_flush_case() !=
          envoy::config::bootstrap::v3::Bootstrap::STATS_FLUSH_NOT_SET) {
    throw EnvoyException("Only one of stats_flush_interval or stats_flush_on_admin should be set!");
  }

  flush_interval_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(bootstrap, stats_flush_interval, 5000));

  if (bootstrap.stats_flush_case() == envoy::config::bootstrap::v3::Bootstrap::kStatsFlushOnAdmin) {
    flush_on_admin_ = bootstrap.stats_flush_on_admin();
  }
}

void MainImpl::initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                          Instance& server,
                          Upstream::ClusterManagerFactory& cluster_manager_factory) {
  // In order to support dynamic configuration of tracing providers,
  // a former server-wide HttpTracer singleton has been replaced by
  // an HttpTracer instance per "envoy.filters.network.http_connection_manager" filter.
  // Tracing configuration as part of bootstrap config is still supported,
  // however, it's become mandatory to process it prior to static Listeners.
  // Otherwise, static Listeners will be configured in assumption that
  // tracing configuration is missing from the bootstrap config.
  initializeTracers(bootstrap.tracing(), server);

  const auto& secrets = bootstrap.static_resources().secrets();
  ENVOY_LOG(info, "loading {} static secret(s)", secrets.size());
  for (ssize_t i = 0; i < secrets.size(); i++) {
    ENVOY_LOG(debug, "static secret #{}: {}", i, secrets[i].name());
    server.secretManager().addStaticSecret(secrets[i]);
  }

  ENVOY_LOG(info, "loading {} cluster(s)", bootstrap.static_resources().clusters().size());
  cluster_manager_ = cluster_manager_factory.clusterManagerFromProto(bootstrap);

  const auto& listeners = bootstrap.static_resources().listeners();
  ENVOY_LOG(info, "loading {} listener(s)", listeners.size());
  for (ssize_t i = 0; i < listeners.size(); i++) {
    ENVOY_LOG(debug, "listener #{}:", i);
    server.listenerManager().addOrUpdateListener(listeners[i], "", false);
  }

  initializeWatchdogs(bootstrap, server);
  initializeStatsConfig(bootstrap, server);
}

void MainImpl::initializeStatsConfig(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                     Instance& server) {
  ENVOY_LOG(info, "loading stats configuration");

  // stats_config_ should be set before populating the sinks so that it is available
  // from the ServerFactoryContext when creating the stats sinks.
  stats_config_ = std::make_unique<StatsConfigImpl>(bootstrap);

  for (const envoy::config::metrics::v3::StatsSink& sink_object : bootstrap.stats_sinks()) {
    // Generate factory and translate stats sink custom config.
    auto& factory = Config::Utility::getAndCheckFactory<StatsSinkFactory>(sink_object);
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        sink_object, server.messageValidationContext().staticValidationVisitor(), factory);

    stats_config_->addSink(factory.createStatsSink(*message, server.serverFactoryContext()));
  }
}

void MainImpl::initializeTracers(const envoy::config::trace::v3::Tracing& configuration,
                                 Instance& server) {
  ENVOY_LOG(info, "loading tracing configuration");

  // Default tracing configuration must be set prior to processing of static Listeners begins.
  server.setDefaultTracingConfig(configuration);

  if (!configuration.has_http()) {
    return;
  }

  // Validating tracing configuration (minimally).
  ENVOY_LOG(info, "  validating default server-wide tracing driver: {}",
            configuration.http().name());

  // Now see if there is a factory that will accept the config.
  auto& factory = Config::Utility::getAndCheckFactory<TracerFactory>(configuration.http());
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      configuration.http(), server.messageValidationContext().staticValidationVisitor(), factory);

  // Notice that the actual HttpTracer instance will be created on demand
  // in the context of "envoy.filters.network.http_connection_manager" filter.
  // The side effect of this is that provider-specific configuration
  // is no longer validated in this step.
}

void MainImpl::initializeWatchdogs(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                   Instance& server) {
  if (bootstrap.has_watchdog() && bootstrap.has_watchdogs()) {
    throw EnvoyException("Only one of watchdog or watchdogs should be set!");
  }

  if (bootstrap.has_watchdog()) {
    main_thread_watchdog_ = std::make_unique<WatchdogImpl>(bootstrap.watchdog(), server);
    worker_watchdog_ = std::make_unique<WatchdogImpl>(bootstrap.watchdog(), server);
  } else {
    main_thread_watchdog_ =
        std::make_unique<WatchdogImpl>(bootstrap.watchdogs().main_thread_watchdog(), server);
    worker_watchdog_ =
        std::make_unique<WatchdogImpl>(bootstrap.watchdogs().worker_watchdog(), server);
  }
}

WatchdogImpl::WatchdogImpl(const envoy::config::bootstrap::v3::Watchdog& watchdog,
                           Instance& server) {
  miss_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, miss_timeout, 200));
  megamiss_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, megamiss_timeout, 1000));

  uint64_t kill_timeout = PROTOBUF_GET_MS_OR_DEFAULT(watchdog, kill_timeout, 0);
  const uint64_t max_kill_timeout_jitter =
      PROTOBUF_GET_MS_OR_DEFAULT(watchdog, max_kill_timeout_jitter, 0);

  // Adjust kill timeout if we have skew enabled.
  if (kill_timeout > 0 && max_kill_timeout_jitter > 0) {
    // Increments the kill timeout with a random value in (0, max_skew].
    // We shouldn't have overflow issues due to the range of Duration.
    // This won't be entirely uniform, depending on how large max_skew
    // is relation to uint64.
    kill_timeout += (server.api().randomGenerator().random() % max_kill_timeout_jitter) + 1;
  }

  kill_timeout_ = std::chrono::milliseconds(kill_timeout);
  multikill_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, multikill_timeout, 0));
  multikill_threshold_ = PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(watchdog, multikill_threshold, 0.0);
  actions_ = watchdog.actions();
}

InitialImpl::InitialImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                         const Options& options)
    : enable_deprecated_v2_api_(options.bootstrapVersion() == 2u) {
  const auto& admin = bootstrap.admin();

  admin_.profile_path_ =
      admin.profile_path().empty() ? "/var/log/envoy/envoy.prof" : admin.profile_path();
  if (admin.has_address()) {
    admin_.address_ = Network::Address::resolveProtoAddress(admin.address());
  }
  admin_.socket_options_ = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
  if (!admin.socket_options().empty()) {
    Network::Socket::appendOptions(
        admin_.socket_options_,
        Network::SocketOptionFactory::buildLiteralOptions(admin.socket_options()));
  }

  if (!bootstrap.flags_path().empty()) {
    flags_path_ = bootstrap.flags_path();
  }

  if (bootstrap.has_layered_runtime()) {
    layered_runtime_.MergeFrom(bootstrap.layered_runtime());
    if (layered_runtime_.layers().empty()) {
      layered_runtime_.add_layers()->mutable_admin_layer();
    }
  }
  if (enable_deprecated_v2_api_) {
    auto* enabled_deprecated_v2_api_layer = layered_runtime_.add_layers();
    enabled_deprecated_v2_api_layer->set_name("enabled_deprecated_v2_api (auto-injected)");
    auto* static_layer = enabled_deprecated_v2_api_layer->mutable_static_layer();
    ProtobufWkt::Value val;
    val.set_bool_value(true);
    (*static_layer
          ->mutable_fields())["envoy.test_only.broken_in_production.enable_deprecated_v2_api"] =
        val;
  }
}

void InitialImpl::initAdminAccessLog(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                     Instance& server) {
  const auto& admin = bootstrap.admin();

  for (const auto& access_log : admin.access_log()) {
    AccessLog::InstanceSharedPtr current_access_log =
        AccessLog::AccessLogFactory::fromProto(access_log, server.serverFactoryContext());
    admin_.access_logs_.emplace_back(current_access_log);
  }

  if (!admin.access_log_path().empty()) {
    Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File,
                                          admin.access_log_path()};
    admin_.access_logs_.emplace_back(new Extensions::AccessLoggers::File::FileAccessLog(
        file_info, {}, Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter(),
        server.accessLogManager()));
  }
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
