#include "source/extensions/filters/network/redis_proxy/config.h"

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/common/redis/cluster_refresh_manager_impl.h"
#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/fault_impl.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"
#include "source/extensions/filters/network/redis_proxy/router_impl.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace {
inline void addUniqueClusters(
    absl::flat_hash_set<std::string>& clusters,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route&
        route) {
  clusters.emplace(route.cluster());
  for (auto& mirror : route.request_mirror_policy()) {
    clusters.emplace(mirror.cluster());
  }
  if (route.has_read_command_policy()) {
    clusters.emplace(route.read_command_policy().cluster());
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

  auto filter_config = std::make_shared<ProxyFilterConfig>(
      proto_config, context.scope(), context.drainDecision(), server_context.runtime(),
      server_context.api(), context.serverFactoryContext().timeSource(), cache_manager_factory);
  const Common::Redis::RespProtocolVersion protocol_version = filter_config->protocolVersion();

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes prefix_routes(
      proto_config.prefix_routes());

  // Set the catch-all route from the settings parameters.
  if (prefix_routes.routes_size() == 0 && !prefix_routes.has_catch_all_route()) {
    throw EnvoyException("cannot configure a redis-proxy without any upstream");
  }

  // Validate the pub/sub resubscribe backoff and subscribe-ack timeout at config load. These
  // feed JitteredExponentialBackOffStrategy and the subscribe-ack timer, which require strictly-
  // positive millisecond values with ``base <= max``. The proto only bounds each Duration below
  // (> 0), but a SUB-millisecond value rounds to 0ms here and an inverted base/max pair passes
  // proto validation — either would otherwise load fine and then abort a worker (the strategy's
  // ASSERT, or a ``% 0``) on the first RESP3 subscription. Compare the EFFECTIVE values, defaulting
  // via the shared constexpr values Common::Redis::Client::ConfigImpl uses (kDefault*Ms) so the
  // validator can never drift from the runtime. Same fail-at-config-load posture as the
  // custom_commands pub/sub guard below.
  const auto& pubsub_settings = proto_config.settings().pubsub_settings();
  const int64_t subscribe_ack_timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(
      pubsub_settings, subscribe_ack_timeout, Common::Redis::Client::kDefaultSubscribeAckTimeoutMs);
  const int64_t resubscribe_backoff_base_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(pubsub_settings, resubscribe_backoff_base_interval,
                                 Common::Redis::Client::kDefaultResubscribeBackoffBaseMs);
  const int64_t resubscribe_backoff_max_ms =
      PROTOBUF_GET_MS_OR_DEFAULT(pubsub_settings, resubscribe_backoff_max_interval,
                                 Common::Redis::Client::kDefaultResubscribeBackoffMaxMs);
  if (subscribe_ack_timeout_ms <= 0 || resubscribe_backoff_base_ms <= 0 ||
      resubscribe_backoff_max_ms <= 0) {
    throw EnvoyException(fmt::format(
        "redis_proxy: pubsub_settings durations must each be at least 1ms (subscribe_ack_timeout="
        "{}ms, resubscribe_backoff_base_interval={}ms, resubscribe_backoff_max_interval={}ms)",
        subscribe_ack_timeout_ms, resubscribe_backoff_base_ms, resubscribe_backoff_max_ms));
  }
  if (resubscribe_backoff_base_ms > resubscribe_backoff_max_ms) {
    throw EnvoyException(fmt::format(
        "redis_proxy: pubsub_settings.resubscribe_backoff_base_interval ({}ms) must not exceed "
        "resubscribe_backoff_max_interval ({}ms)",
        resubscribe_backoff_base_ms, resubscribe_backoff_max_ms));
  }
  // cap each duration at 1 hour. These feed deadline arithmetic — the subscribe-ack scheduler
  // computes ``monotonicTime() + timeout_`` (a MonotonicTime point), and the generation / backoff
  // timers arm on these values — so an extreme value (PGV's Duration validator allows up to ~292
  // years) would overflow the time-point's signed nanosecond representation (undefined behavior)
  // and could wrap to a PAST deadline, making a healthy upstream time out, roll back, and close
  // immediately. One hour is far above any sane ack or backoff window, matching the fail-at-load
  // spirit of the >0 / base<=max checks above.
  constexpr int64_t kMaxPubsubDurationMs = 60 * 60 * 1000; // 1 hour
  if (subscribe_ack_timeout_ms > kMaxPubsubDurationMs ||
      resubscribe_backoff_base_ms > kMaxPubsubDurationMs ||
      resubscribe_backoff_max_ms > kMaxPubsubDurationMs) {
    throw EnvoyException(
        fmt::format("redis_proxy: pubsub_settings durations must each be at most {}ms / 1h "
                    "(subscribe_ack_timeout={}ms, resubscribe_backoff_base_interval={}ms, "
                    "resubscribe_backoff_max_interval={}ms)",
                    kMaxPubsubDurationMs, subscribe_ack_timeout_ms, resubscribe_backoff_base_ms,
                    resubscribe_backoff_max_ms));
  }

  absl::flat_hash_set<std::string> unique_clusters;
  for (auto& route : prefix_routes.routes()) {
    addUniqueClusters(unique_clusters, route);
  }
  addUniqueClusters(unique_clusters, prefix_routes.catch_all_route());

  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(context.scope().symbolTable());

  Upstreams upstreams;
  for (auto& cluster : unique_clusters) {

    // Create the AWS IAM authenticator if required
    std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator;
    std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config;
    auto cluster_optref = server_context.clusterManager().clusters().getCluster(cluster);
    if (cluster_optref.has_value()) {
      // Does our cluster have an AwsIam element available? If so, create a new authenticator for
      // this connection pool.
      aws_iam_config = ProtocolOptionsConfigImpl::awsIamConfig(cluster_optref.value().get().info());
      if (aws_iam_config.has_value()) {
        if (!ProtocolOptionsConfigImpl::authUsername(cluster_optref.value().get().info(),
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
        local_zone, protocol_version);
    conn_pool_ptr->init();
    upstreams.emplace(cluster, conn_pool_ptr);
  }

  auto router =
      std::make_unique<PrefixRoutes>(prefix_routes, std::move(upstreams), server_context.runtime());

  auto fault_manager = std::make_unique<Common::Redis::FaultManagerImpl>(
      server_context.api().randomGenerator(), server_context.runtime(), proto_config.faults());

  absl::flat_hash_set<std::string> custom_commands;
  for (const auto& cmd : proto_config.custom_commands()) {
    // Reject custom_commands that would silently shadow the proxy's built-in pub/sub handlers. The
    // command registry overwrites on insert and custom commands register last, so an
    // entry like ``custom_commands: [publish]`` would replace the pub/sub handler with the generic
    // pass-through — defeating the sharded rewrite so a PUBLISH no longer reaches SSUBSCRIBE
    // subscribers — while ``[subscribe]`` (or the pattern verb ``[psubscribe]``, or an internal
    // sharded verb ``[ssubscribe]``) would send a raw command down a DATA connection where it hangs
    // with no Push reply until the op timeout closes that shared connection. The authoritative
    // owned set is ``SupportedCommands::pubSubCommands()`` (one source of truth for this guard), a
    // superset of the client-exposed ``subscriptionCommands()`` the splitter registers — so pattern
    // verbs (PSUBSCRIBE/PUNSUBSCRIBE) and the internal sharded verbs (SSUBSCRIBE/SUNSUBSCRIBE) are
    // covered too, not just SUBSCRIBE/UNSUBSCRIBE/PUBLISH/SPUBLISH. Compared case-insensitively so
    // ``[PUBLISH]`` is rejected too, even though its wrong case would not actually match a handler.
    const std::string lowercase_cmd = absl::AsciiStrToLower(cmd);
    if (Common::Redis::SupportedCommands::pubSubCommands().contains(lowercase_cmd)) {
      throw EnvoyException(fmt::format(
          "redis_proxy: custom_commands cannot override the built-in pub/sub command '{}'", cmd));
    }
    custom_commands.insert(cmd);
  }

  // the sharded SUBSCRIBE rewrite is on unless the operator explicitly disables it for a RESP3
  // upstream that lacks SSUBSCRIBE (Redis 6.x). ``pubsub_settings`` was read above for the backoff
  // validation.
  const bool enable_sharded_subscribe = pubsub_settings.sharded_subscription_mode() !=
                                        envoy::extensions::filters::network::redis_proxy::v3::
                                            RedisProxy::ConnPoolSettings::PubsubSettings::DISABLED;
  // the PUBLISH -> SPUBLISH rewrite is gated ONLY by the listener flag
  // enable_sharded_publish, independent of the subscription mode. So DISABLED sharded subscribe
  // (the Redis 6.x escape hatch: the upstream has no SSUBSCRIBE) combined with
  // enable_sharded_publish rewrites every PUBLISH to an SPUBLISH the 6.x upstream then rejects. A
  // hard NACK would be wrong — on a 7.x upstream DISABLED can be a deliberate "no sharded
  // subscribe" policy switch under which SPUBLISH is still valid — so warn at load and
  // cross-reference both fields' docs rather than reject.
  if (proto_config.enable_sharded_publish() && !enable_sharded_subscribe) {
    ENVOY_LOG_MISC(
        warn,
        "redis_proxy: enable_sharded_publish is set while sharded_subscription_mode is DISABLED. "
        "PUBLISH is rewritten to SPUBLISH regardless of the subscription mode, so a Redis 6.x "
        "upstream (which lacks SSUBSCRIBE/SPUBLISH) will reject every PUBLISH. Disable "
        "enable_sharded_publish for a 6.x upstream; ignore this if the upstream is 7.x+ and "
        "DISABLED "
        "is an intentional no-sharded-subscribe policy.");
  }
  std::shared_ptr<CommandSplitter::Instance> splitter =
      std::make_shared<CommandSplitter::InstanceImpl>(
          std::move(router), context.scope(), filter_config->stat_prefix_,
          server_context.timeSource(), proto_config.latency_in_micros(), std::move(fault_manager),
          std::move(custom_commands), enable_sharded_subscribe);

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
