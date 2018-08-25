#include "server/configuration_impl.h"

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/trace/v2/trace.pb.h"
#include "envoy/network/connection.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/lds_json.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/ratelimit/ratelimit_impl.h"
#include "common/tracing/http_tracer_impl.h"

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

void MainImpl::initialize(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                          Instance& server,
                          Upstream::ClusterManagerFactory& cluster_manager_factory) {
  const auto& secrets = bootstrap.static_resources().secrets();
  ENVOY_LOG(info, "loading {} static secret(s)", secrets.size());
  for (ssize_t i = 0; i < secrets.size(); i++) {
    ENVOY_LOG(debug, "static secret #{}: {}", i, secrets[i].name());
    server.secretManager().addStaticSecret(secrets[i]);
  }

  ENVOY_LOG(info, "loading {} cluster(s)", bootstrap.static_resources().clusters().size());
  cluster_manager_ = cluster_manager_factory.clusterManagerFromProto(
      bootstrap, server.stats(), server.threadLocal(), server.runtime(), server.random(),
      server.localInfo(), server.accessLogManager(), server.admin());
  const auto& listeners = bootstrap.static_resources().listeners();
  ENVOY_LOG(info, "loading {} listener(s)", listeners.size());
  for (ssize_t i = 0; i < listeners.size(); i++) {
    ENVOY_LOG(debug, "listener #{}:", i);
    server.listenerManager().addOrUpdateListener(listeners[i], "", false);
  }

  stats_flush_interval_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(bootstrap, stats_flush_interval, 5000));

  const auto& watchdog = bootstrap.watchdog();
  watchdog_miss_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, miss_timeout, 200));
  watchdog_megamiss_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, megamiss_timeout, 1000));
  watchdog_kill_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, kill_timeout, 0));
  watchdog_multikill_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(watchdog, multikill_timeout, 0));

  initializeTracers(bootstrap.tracing(), server);

  if (bootstrap.has_rate_limit_service()) {
    ratelimit_client_factory_.reset(
        new RateLimit::GrpcFactoryImpl(bootstrap.rate_limit_service(),
                                       cluster_manager_->grpcAsyncClientManager(), server.stats()));
  } else {
    ratelimit_client_factory_.reset(new RateLimit::NullFactoryImpl());
  }

  initializeStatsSinks(bootstrap, server);
}

void MainImpl::initializeTracers(const envoy::config::trace::v2::Tracing& configuration,
                                 Instance& server) {
  ENVOY_LOG(info, "loading tracing configuration");

  if (!configuration.has_http()) {
    http_tracer_.reset(new Tracing::HttpNullTracer());
    return;
  }

  // Initialize tracing driver.
  std::string type = configuration.http().name();
  ENVOY_LOG(info, "  loading tracing driver: {}", type);

  // TODO(htuch): Make this dynamically pluggable one day.
  Json::ObjectSharedPtr driver_config =
      MessageUtil::getJsonObjectFromMessage(configuration.http().config());

  // Now see if there is a factory that will accept the config.
  auto& factory = Config::Utility::getAndCheckFactory<TracerFactory>(type);
  http_tracer_ = factory.createHttpTracer(*driver_config, server);
}

void MainImpl::initializeStatsSinks(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                                    Instance& server) {
  ENVOY_LOG(info, "loading stats sink configuration");

  for (const envoy::config::metrics::v2::StatsSink& sink_object : bootstrap.stats_sinks()) {
    // Generate factory and translate stats sink custom config
    auto& factory = Config::Utility::getAndCheckFactory<StatsSinkFactory>(sink_object.name());
    ProtobufTypes::MessagePtr message =
        Config::Utility::translateToFactoryConfig(sink_object, factory);

    stats_sinks_.emplace_back(factory.createStatsSink(*message, server));
  }
}

InitialImpl::InitialImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  const auto& admin = bootstrap.admin();
  admin_.access_log_path_ = admin.access_log_path();
  admin_.profile_path_ =
      admin.profile_path().empty() ? "/var/log/envoy/envoy.prof" : admin.profile_path();
  admin_.address_ = Network::Address::resolveProtoAddress(admin.address());

  if (!bootstrap.flags_path().empty()) {
    flags_path_ = bootstrap.flags_path();
  }

  if (bootstrap.has_runtime()) {
    runtime_.reset(new RuntimeImpl());
    runtime_->symlink_root_ = bootstrap.runtime().symlink_root();
    runtime_->subdirectory_ = bootstrap.runtime().subdirectory();
    runtime_->override_subdirectory_ = bootstrap.runtime().override_subdirectory();
  }
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
