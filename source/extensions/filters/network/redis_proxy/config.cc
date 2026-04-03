#include "source/extensions/filters/network/redis_proxy/config.h"

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/common/redis/cluster_refresh_manager_impl.h"
#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/fault_impl.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"
#include "source/extensions/filters/network/redis_proxy/router_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace {
// Response-bearing clusters: those whose responses reach the downstream
// client. These constrain the listener-level RESP cap. Excludes mirror
// clusters — their responses are fire-and-forget and cannot affect the
// downstream protocol shape.
inline void addResponseBearingClusters(
    absl::flat_hash_set<std::string>& clusters,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route&
        route) {
  clusters.emplace(route.cluster());
  if (route.has_read_command_policy()) {
    clusters.emplace(route.read_command_policy().cluster());
  }
}

// All referenced clusters (response-bearing + mirrors). Used to drive
// upstream connection-pool creation; mirrors need pools too even though
// they do not constrain the RESP cap.
inline void addAllReferencedClusters(
    absl::flat_hash_set<std::string>& clusters,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route&
        route) {
  addResponseBearingClusters(clusters, route);
  for (auto& mirror : route.request_mirror_policy()) {
    clusters.emplace(mirror.cluster());
  }
}
} // namespace

Network::FilterFactoryCb RedisProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  auto& server_context = context.serverFactoryContext();

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(proto_config.has_settings());

  Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager =
      Extensions::Common::Redis::getClusterRefreshManager(
          server_context.singletonManager(), server_context.mainThreadDispatcher(),
          server_context.clusterManager(), server_context.timeSource());

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context);

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes prefix_routes(
      proto_config.prefix_routes());

  // Set the catch-all route from the settings parameters.
  if (prefix_routes.routes_size() == 0 && !prefix_routes.has_catch_all_route()) {
    throw EnvoyException("cannot configure a redis-proxy without any upstream");
  }

  absl::flat_hash_set<std::string> unique_clusters;
  absl::flat_hash_set<std::string> response_bearing_set;
  for (auto& route : prefix_routes.routes()) {
    addAllReferencedClusters(unique_clusters, route);
    addResponseBearingClusters(response_bearing_set, route);
  }
  // Only fold the catch-all route into the unique-cluster set when one is
  // actually configured. ``prefix_routes.catch_all_route()`` returns a
  // default-constructed Route message when has_catch_all_route() is false,
  // which would insert "" into unique_clusters and try to allocate an
  // upstream pool keyed on the empty string.
  if (prefix_routes.has_catch_all_route()) {
    addAllReferencedClusters(unique_clusters, prefix_routes.catch_all_route());
    addResponseBearingClusters(response_bearing_set, prefix_routes.catch_all_route());
  }

  // Resolve each cluster's info up front and capture the cluster ``OptRef``
  // (input to AWS IAM lookup below). One ClusterManager lookup per cluster —
  // the per-conn-pool loop reuses the captured ``OptRef`` instead of re-resolving.
  // The listener-level RESP cap is no longer pre-computed here; it is read
  // dynamically per HELLO and per command via
  // ProxyFilterConfig::clusterRespVersion(), which consults the worker's live
  // cluster manager state. This means cluster updates (LDS-before-CDS,
  // RESP3→RESP2 downgrade) are reflected without listener reload.
  struct ResolvedCluster {
    std::string name;
    Upstream::ClusterConstOptRef optref;
  };
  std::vector<ResolvedCluster> resolved_clusters;
  resolved_clusters.reserve(unique_clusters.size());
  for (const auto& cluster_name : unique_clusters) {
    auto cluster_optref = server_context.clusterManager().clusters().getCluster(cluster_name);
    resolved_clusters.push_back({cluster_name, cluster_optref});
  }

  // Hand the response-bearing cluster names to the config so
  // clusterRespVersion() can compute the floor at request time.
  std::vector<std::string> response_bearing_clusters(response_bearing_set.begin(),
                                                     response_bearing_set.end());

  auto filter_config = std::make_shared<ProxyFilterConfig>(
      proto_config, context.scope(), context.drainDecision(), server_context.runtime(),
      server_context.api(), context.serverFactoryContext().timeSource(), cache_manager_factory,
      server_context.clusterManager(), std::move(response_bearing_clusters));

  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(context.scope().symbolTable());

  Upstreams upstreams;
  for (const auto& resolved : resolved_clusters) {
    const std::string& cluster = resolved.name;

    // Create the AWS IAM authenticator if required
    absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator;
    absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config;
    if (resolved.optref.has_value()) {
      // Does our cluster have an AwsIam element available? If so, create a new authenticator for
      // this connection pool.
      aws_iam_config =
          ProtocolOptionsConfigImpl::awsIamConfig(resolved.optref.value().get().info());
      if (aws_iam_config.has_value()) {
        if (!ProtocolOptionsConfigImpl::authUsername(resolved.optref.value().get().info(),
                                                     context.serverFactoryContext().api())
                 .empty()) {
          aws_iam_authenticator = Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorFactory::
              initAwsIamAuthenticator(server_context, aws_iam_config.value());
        } else {
          ENVOY_LOG_MISC(warn,
                         "No auth_username found for cluster {}, AWS IAM Authentication will be "
                         "disabled for this cluster",
                         cluster);
        }
      }
    }

    Stats::ScopeSharedPtr stats_scope =
        context.scope().createScope(fmt::format("cluster.{}.redis_cluster", cluster));
    // Get zone from LocalInfo for fallback when client_zone not explicitly configured
    const std::string& local_zone = server_context.localInfo().zoneName();
    auto conn_pool_ptr = std::make_shared<ConnPool::InstanceImpl>(
        cluster, server_context.clusterManager(),
        Common::Redis::Client::ClientFactoryImpl::instance_, server_context.threadLocal(),
        proto_config.settings(), server_context.api(), std::move(stats_scope), redis_command_stats,
        refresh_manager, filter_config->dns_cache_, aws_iam_config, aws_iam_authenticator,
        local_zone);
    // Upstream RESP version is read from the cluster's RedisProtocolOptions
    // inside InstanceImpl::ThreadLocalPool::onClusterAddOrUpdateNonVirtual,
    // so no filter-level push is needed here.
    conn_pool_ptr->init();
    upstreams.emplace(cluster, conn_pool_ptr);
  }

  auto router =
      std::make_unique<PrefixRoutes>(prefix_routes, std::move(upstreams), server_context.runtime());

  auto fault_manager = std::make_unique<Common::Redis::FaultManagerImpl>(
      server_context.api().randomGenerator(), server_context.runtime(), proto_config.faults());

  absl::flat_hash_set<std::string> custom_commands;
  for (const auto& cmd : proto_config.custom_commands()) {
    custom_commands.insert(cmd);
  }

  std::shared_ptr<CommandSplitter::Instance> splitter =
      std::make_shared<CommandSplitter::InstanceImpl>(
          std::move(router), context.scope(), filter_config->stat_prefix_,
          server_context.timeSource(), proto_config.latency_in_micros(), std::move(fault_manager),
          std::move(custom_commands));

  auto has_external_auth_provider_ = proto_config.has_external_auth_provider();
  auto grpc_service = proto_config.external_auth_provider().grpc_service();
  auto timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(grpc_service, timeout, 200);

  return [has_external_auth_provider_, grpc_service, &context, splitter, filter_config,
          timeout_ms](Network::FilterManager& filter_manager) -> void {
    Common::Redis::DecoderFactoryImpl decoder_factory;

    ExternalAuth::ExternalAuthClientPtr&& auth_client{nullptr};
    if (has_external_auth_provider_) {
      auto auth_client_factory_or_error =
          context.serverFactoryContext()
              .clusterManager()
              .grpcAsyncClientManager()
              .factoryForGrpcService(grpc_service, context.scope(), true);
      THROW_IF_NOT_OK_REF(auth_client_factory_or_error.status());

      auth_client = std::make_unique<ExternalAuth::GrpcExternalAuthClient>(
          THROW_OR_RETURN_VALUE(
              auth_client_factory_or_error.value()->createUncachedRawAsyncClient(),
              Grpc::RawAsyncClientPtr),
          std::chrono::milliseconds(timeout_ms));
    }

    filter_manager.addReadFilter(std::make_shared<ProxyFilter>(
        decoder_factory, Common::Redis::EncoderPtr{new Common::Redis::EncoderImpl()}, *splitter,
        filter_config, std::move(auth_client)));
  };
}

/**
 * Static registration for the redis filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(RedisProxyFilterConfigFactory,
                        Server::Configuration::NamedNetworkFilterConfigFactory,
                        "envoy.redis_proxy");

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
