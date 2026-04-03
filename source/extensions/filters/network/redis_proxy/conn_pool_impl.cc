#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/stats/utility.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {
namespace {
// Shared no-op callbacks for fire-and-forget requests (asking, etc.).
Common::Redis::Client::DoNothingPoolCallbacks null_client_callbacks;
const Common::Redis::RespValue& getRequest(const RespVariant& request) {
  if (request.index() == 0) {
    return absl::get<const Common::Redis::RespValue>(request);
  } else {
    return *(absl::get<Common::Redis::RespValueConstSharedPtr>(request));
  }
}

static uint16_t default_port = 6379;

} // namespace

InstanceImpl::InstanceImpl(
    const std::string& cluster_name, Upstream::ClusterManager& cm,
    Common::Redis::Client::ClientFactory& client_factory, ThreadLocal::SlotAllocator& tls,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings&
        config,
    Api::Api& api, Stats::ScopeSharedPtr&& stats_scope,
    const Common::Redis::RedisCommandStatsSharedPtr& redis_command_stats,
    Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager,
    const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr& dns_cache,
    std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator,
    const std::string& local_zone, Common::Redis::RespProtocolVersion protocol_version)
    : cluster_name_(cluster_name), cm_(cm), client_factory_(client_factory),
      tls_(tls.allocateSlot()), config_(new Common::Redis::Client::ConfigImpl(config)), api_(api),
      stats_scope_(std::move(stats_scope)), redis_command_stats_(redis_command_stats),
      redis_cluster_stats_{REDIS_CLUSTER_STATS(POOL_COUNTER(*stats_scope_))},
      refresh_manager_(std::move(refresh_manager)), dns_cache_(dns_cache),
      aws_iam_authenticator_(aws_iam_authenticator), aws_iam_config_(aws_iam_config),
      local_zone_(local_zone), protocol_version_(protocol_version) {}

void InstanceImpl::init() {
  // Note: `this` and `cluster_name` have a a lifetime of the filter.
  // That may be shorter than the tls callback if the listener is torn down shortly after it is
  // created. We use a weak pointer to make sure this object outlives the tls callbacks.
  std::weak_ptr<InstanceImpl> this_weak_ptr = this->shared_from_this();
  tls_->set(
      [this_weak_ptr](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        if (auto this_shared_ptr = this_weak_ptr.lock()) {
          return std::make_shared<ThreadLocalPool>(
              this_shared_ptr, dispatcher, this_shared_ptr->cluster_name_, this_shared_ptr->api_,
              this_shared_ptr->dns_cache_, this_shared_ptr->aws_iam_config_,
              this_shared_ptr->aws_iam_authenticator_);
        }
        return nullptr;
      });
}

uint16_t InstanceImpl::shardSize() { return tls_->getTyped<ThreadLocalPool>().shardSize(); }

// This method is always called from a InstanceSharedPtr we don't have to worry about tls_->getTyped
// failing due to InstanceImpl going away.
Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequest(const std::string& key, RespVariant&& request, PoolCallbacks& callbacks,
                          Common::Redis::Client::Transaction& transaction) {
  return tls_->getTyped<ThreadLocalPool>().makeRequest(key, std::move(request), callbacks,
                                                       transaction);
}

// This method is always called from a InstanceSharedPtr we don't have to worry about tls_->getTyped
// failing due to InstanceImpl going away.
Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequestToHost(const std::string& host_address,
                                const Common::Redis::RespValue& request,
                                Common::Redis::Client::ClientCallbacks& callbacks) {
  return tls_->getTyped<ThreadLocalPool>().makeRequestToHost(host_address, request, callbacks);
}

// This method is always called from a InstanceSharedPtr we don't have to worry about tls_->getTyped
// failing due to InstanceImpl going away.
Common::Redis::Client::PoolRequest*
InstanceImpl::makeRequestToShard(uint16_t shard_index, RespVariant&& request,
                                 PoolCallbacks& callbacks,
                                 Common::Redis::Client::Transaction& transaction) {
  return tls_->getTyped<ThreadLocalPool>().makeRequestToShard(shard_index, std::move(request),
                                                              callbacks, transaction);
}

InstanceImpl::ThreadLocalPool::ThreadLocalPool(
    std::shared_ptr<InstanceImpl> parent, Event::Dispatcher& dispatcher, std::string cluster_name,
    Api::Api& api, const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr& dns_cache,
    std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator)
    : parent_(parent), dispatcher_(dispatcher), cluster_name_(std::move(cluster_name)), api_(api),
      dns_cache_(dns_cache),
      drain_timer_(dispatcher.createTimer([this]() -> void { drainClients(); })),
      client_factory_(parent->client_factory_), config_(parent->config_),
      stats_scope_(parent->stats_scope_), redis_command_stats_(parent->redis_command_stats_),
      redis_cluster_stats_(parent->redis_cluster_stats_),
      refresh_manager_(parent->refresh_manager_), aws_iam_authenticator_(aws_iam_authenticator),
      aws_iam_config_(aws_iam_config), client_zone_(parent->localZone()),
      upstream_protocol_version_(parent->protocol_version_) {

  cluster_update_handle_ = parent->cm_.addThreadLocalClusterUpdateCallbacks(*this);
  Upstream::ThreadLocalCluster* cluster = parent->cm_.getThreadLocalCluster(cluster_name_);
  if (cluster != nullptr) {
    Upstream::ThreadLocalClusterCommand command = [&cluster]() -> Upstream::ThreadLocalCluster& {
      return *cluster;
    };
    onClusterAddOrUpdateNonVirtual(cluster->info()->name(), command);
  }
}

InstanceImpl::ThreadLocalPool::~ThreadLocalPool() {
  if (resubscribe_timer_) {
    resubscribe_timer_->disableTimer();
  }
  // Tear the subscription registry down BEFORE closing clients. Closing a subscription client
  // synchronously drives ThreadLocalActiveClient::onEvent, which — while the registry is still
  // populated — seeds the retry scope synchronously and posts ``[weak_registry]{
  // armResubscribeBackoff() }`` (a WEAK capture — MISC-3). That posted lambda can outlive this
  // pool; were it to run, armResubscribeBackoff → scheduleResubscribe would touch the freed pool
  // through upstream_callbacks_ (a bare reference to *this) — a use-after-free. The weak capture
  // means it no-ops once reset() below drops our ref (its lock() fails), and clear() empties the
  // maps so a lambda that still races us via some surviving strong ref finds an empty registry and
  // re-issues nothing before touching *this.
  if (subscription_registry_) {
    subscription_registry_->clear();
    subscription_registry_.reset();
  }
  while (!pending_requests_.empty()) {
    pending_requests_.pop_front();
  }
  closeAllClients();
}

void InstanceImpl::ThreadLocalPool::closeAllClients() {
  while (!subscription_client_map_.empty()) {
    subscription_client_map_.begin()->second->redis_client_->close();
  }
  while (!client_map_.empty()) {
    client_map_.begin()->second->redis_client_->close();
  }
  while (!clients_to_drain_.empty()) {
    (*clients_to_drain_.begin())->redis_client_->close();
  }
}

void InstanceImpl::ThreadLocalPool::onClusterAddOrUpdateNonVirtual(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  if (cluster_name != cluster_name_) {
    return;
  }
  // Ensure the filter is not deleted in the main thread during this method.
  auto shared_parent = parent_.lock();
  if (!shared_parent) {
    return;
  }

  if (cluster_ != nullptr) {
    // Treat an update as a removal followed by an add.
    ThreadLocalPool::onClusterRemoval(cluster_name_);
  }

  ASSERT(cluster_ == nullptr);
  auto& cluster = get_cluster();
  cluster_ = &cluster;
  // Update username and password when cluster updates. authPassword is ignored by the client when
  // AWS IAM Authentication is enabled.
  auth_username_ = ProtocolOptionsConfigImpl::authUsername(cluster_->info(), api_);
  auth_password_ = ProtocolOptionsConfigImpl::authPassword(cluster_->info(), api_);
  // upstream_protocol_version_ is set once at ThreadLocalPool construction from
  // the filter-level configuration; cluster updates do not flip it.
  ASSERT(host_set_member_update_cb_handle_ == nullptr);
  host_set_member_update_cb_handle_ = cluster_->prioritySet().addMemberUpdateCb(
      [this](const std::vector<Upstream::HostSharedPtr>& hosts_added,
             const std::vector<Upstream::HostSharedPtr>& hosts_removed) {
        onHostsAdded(hosts_added);
        onHostsRemoved(hosts_removed);
        // Notify the subscription registry of a topology change so per-shard subscriptions can be
        // re-routed to their correct shards. This must fire not only on host add/remove but ALSO on
        // a slot-only rebalance — a channel's hash slot migrating between EXISTING nodes with no
        // host delta (Redis resharding / CLUSTER SETSLOT). RedisCluster::onClusterSlotUpdate calls
        // updateAllHosts({}, {}) — firing this callback with empty deltas — whenever the slot map
        // changed, so gating on a non-empty host delta stranded the resharded subscription on its
        // old owner (it stopped receiving messages). updateAllHosts runs only when hosts OR slots
        // actually changed, so this posts onClusterTopologyChange only on real topology changes (a
        // no-op when there are no subscriptions); that handler walks the whole subscription set and
        // re-routes each channel to its current slot owner. Defer to the next event loop iteration
        // — the load balancer hasn't been recreated yet here, so chooseHost() would otherwise use
        // the stale slot mapping.
        if (subscription_registry_ != nullptr) {
          // Deferred + weak-captured (MISC-3) — see postToRegistry.
          postToRegistry(subscription_registry_, [](SubscriptionRegistry& registry) {
            registry.onClusterTopologyChange();
          });
        }
      });

  ASSERT(host_address_map_.empty());
  for (const auto& i : cluster_->prioritySet().hostSetsPerPriority()) {
    for (auto& host : i->hosts()) {
      host_address_map_[host->address()->asString()] = host;
    }
  }

  // Figure out if the cluster associated with this ConnPool is a Redis cluster
  // with its own hash slot sharding scheme and ability to dynamically discover
  // its members. This is done once to minimize overhead in the data path, makeRequest() in
  // particular.
  Upstream::ClusterInfoConstSharedPtr info = cluster_->info();
  OptRef<const envoy::config::cluster::v3::Cluster::CustomClusterType> cluster_type =
      info->clusterType();
  is_redis_cluster_ = cluster_type.has_value() && cluster_type->name() == "envoy.clusters.redis";
}

void InstanceImpl::ThreadLocalPool::onClusterRemoval(absl::string_view cluster_name) {
  if (cluster_name != cluster_name_) {
    return;
  }

  // Treat cluster removal as a removal of all hosts. Close all connections and fail all pending
  // requests.
  host_set_member_update_cb_handle_ = nullptr;
  // Clear the subscription registry BEFORE closing subscription connections so the close-driven
  // onEvent finds it empty and its posted onUpstreamConnectionClose no-ops (the cluster is gone —
  // there is nowhere to resubscribe).
  if (subscription_registry_) {
    subscription_registry_->clear();
    subscription_registry_.reset();
  }
  closeAllClients();

  cluster_ = nullptr;
  host_address_map_.clear();
  cx_rate_limiter_map_.clear();
  // Disable the timer AFTER resetting subscription_registry_ above: the timer's callback reads
  // subscription_registry_, which is now null, so even a fire that races this teardown no-ops.
  if (resubscribe_timer_) {
    resubscribe_timer_->disableTimer();
  }
}

void InstanceImpl::ThreadLocalPool::onHostsAdded(
    const std::vector<Upstream::HostSharedPtr>& hosts_added) {
  for (const auto& host : hosts_added) {
    std::string host_address = host->address()->asString();
    // Insert new host into address map, possibly overwriting a previous host's entry.
    host_address_map_[host_address] = host;
    for (const auto& created_host : created_via_redirect_hosts_) {
      if (created_host->address()->asString() == host_address) {
        // Remove our "temporary" host created in makeRequestToHost().
        onHostsRemoved({created_host});
        created_via_redirect_hosts_.remove(created_host);
        break;
      }
    }
  }
}

void InstanceImpl::ThreadLocalPool::onHostsRemoved(
    const std::vector<Upstream::HostSharedPtr>& hosts_removed) {
  for (const auto& host : hosts_removed) {
    auto token_bucket = cx_rate_limiter_map_.find(host);
    if (token_bucket != cx_rate_limiter_map_.end()) {
      cx_rate_limiter_map_.erase(token_bucket);
    }

    // Forget any per-shard channel→host mapping pointing at the removed host BEFORE the posted
    // onClusterTopologyChange runs, so the registry never aims a SUNSUBSCRIBE at the gone host
    // (which threadLocalActiveClient would satisfy by opening a connection to a dead endpoint).
    // The channels stay subscribed and are re-routed to their new owner by the resubscribe path.
    if (subscription_registry_) {
      subscription_registry_->dropHost(host);
    }

    // Close the removed host's dedicated subscription connection (if any). Mark the host as a
    // PLANNED removal first: the close drives ThreadLocalActiveClient::onEvent, which for a genuine
    // connection loss posts onUpstreamConnectionClose to resubscribe. But this is a planned removal
    // — the onClusterTopologyChange that this same member-update callback posts already re-routes
    // the moved channels to their new owner (with its own failure fallback), so a resubscribe from
    // onEvent as well would be a second, redundant round (C-6). onEvent consumes the mark and skips
    // its post. (We also do NOT post onUpstreamConnectionClose directly here — onEvent owns the
    // single close→cleanup path; see the C-5 note.)
    auto sub_it = subscription_client_map_.find(host);
    if (sub_it != subscription_client_map_.end()) {
      sub_it->second->planned_removal_ = true;
      sub_it->second->redis_client_->close();
    }

    auto it = client_map_.find(host);
    if (it != client_map_.end()) {
      // Data connections in client_map_ never carry push callbacks (only the dedicated
      // subscription_client_map_ clients do — see getOrCreateSubscriptionClient), so there is
      // nothing subscription-related to clear here before the drain check.
      if (it->second->redis_client_->active()) {
        // Put the ThreadLocalActiveClient to the side to drain.
        clients_to_drain_.push_back(std::move(it->second));
        client_map_.erase(it);
        if (!drain_timer_->enabled()) {
          drain_timer_->enableTimer(std::chrono::seconds(1));
        }
      } else {
        // There are no pending requests so close the connection.
        it->second->redis_client_->close();
      }
    }
    // There is the possibility that multiple hosts with the same address
    // are registered in host_address_map_ given that hosts may be created
    // upon redirection or supplied as part of the cluster's definition.
    auto it2 = host_address_map_.find(host->address()->asString());
    if ((it2 != host_address_map_.end()) && (it2->second == host)) {
      host_address_map_.erase(it2);
    }
  }
}

void InstanceImpl::ThreadLocalPool::drainClients() {
  while (!clients_to_drain_.empty() && !(*clients_to_drain_.begin())->redis_client_->active()) {
    (*clients_to_drain_.begin())->redis_client_->close();
  }
  if (!clients_to_drain_.empty()) {
    drain_timer_->enableTimer(std::chrono::seconds(1));
  }
}

InstanceImpl::ThreadLocalActiveClient*
InstanceImpl::ThreadLocalPool::threadLocalActiveClient(Upstream::HostConstSharedPtr host) {
  return getOrCreateClientInMap(client_map_, host);
}

InstanceImpl::ThreadLocalActiveClient*
InstanceImpl::ThreadLocalPool::threadLocalActiveSubscriptionClient(
    Upstream::HostConstSharedPtr host) {
  return getOrCreateClientInMap(subscription_client_map_, host);
}

InstanceImpl::ThreadLocalActiveClient* InstanceImpl::ThreadLocalPool::getOrCreateClientInMap(
    absl::node_hash_map<Upstream::HostConstSharedPtr, ThreadLocalActiveClientPtr>& client_map,
    Upstream::HostConstSharedPtr host) {
  ThreadLocalActiveClientPtr& client = client_map[host];
  if (!client) {
    if (config_->connectionRateLimitEnabled()) {
      // Consult the per-host connection rate limiter only when a NEW client must actually be
      // created AND rate limiting is enabled. Its former position — ``cx_rate_limiter_map_[host]``
      // at the top of the function — operator[]-inserted a null TokenBucket for every host on EVERY
      // call, including the warm path where ``client`` already exists and when rate limiting is
      // disabled entirely, growing the map with dead entries on the hot data path (efficiency). The
      // bucket is created lazily per host on first need and persists across calls so it refills
      // over time.
      TokenBucketPtr& rate_limiter = cx_rate_limiter_map_[host];
      if (!rate_limiter) {
        rate_limiter = std::make_unique<TokenBucketImpl>(config_->connectionRateLimitPerSec(),
                                                         dispatcher_.timeSource(),
                                                         config_->connectionRateLimitPerSec());
      }
      if (rate_limiter->consume(1, false) == 0) {
        redis_cluster_stats_.connection_rate_limited_.inc();
        // The ``client_map[host]`` lookup above inserted a null placeholder. Erase it here — inside
        // the one helper that created it — so no caller has to remember to, and so teardown /
        // onClusterRemoval never walk the map and dereference a null unique_ptr. ``client`` is
        // invalidated by this erase and must not be touched afterward.
        client_map.erase(host);
        return nullptr;
      }
    }
    ASSERT(cluster_ != nullptr);
    const auto credentials =
        ProtocolOptionsConfigImpl::authCredentials(cluster_->info(), api_, host);
    client = std::make_unique<ThreadLocalActiveClient>(*this);
    client->host_ = host;
    client->redis_client_ = client_factory_.create(
        host, dispatcher_, config_, redis_command_stats_, *(stats_scope_), credentials.username,
        credentials.password, false, aws_iam_config_, aws_iam_authenticator_,
        upstream_protocol_version_, makeOptRef(redis_cluster_stats_.upstream_resp3_hello_failure_));

    client->redis_client_->addConnectionCallbacks(*client);
    // RESP3 HELLO 3 negotiation runs inside ClientImpl::initialize (driven by the
    // upstream_protocol_version_ argument passed into create above). User requests
    // dispatched against this client before the handshake completes are held by
    // ClientImpl's held-init queue and replayed in order on negotiation success.
  }
  return client.get();
}

uint16_t InstanceImpl::ThreadLocalPool::shardSize() {
  if (cluster_ == nullptr) {
    ASSERT(client_map_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    return 0;
  }

  Common::Redis::RespValue request;
  absl::flat_hash_set<Upstream::HostConstSharedPtr> unique_hosts;
  unique_hosts.reserve(Envoy::Extensions::Clusters::Redis::MaxSlot);
  for (uint16_t size = 0; size < Envoy::Extensions::Clusters::Redis::MaxSlot; size++) {
    Clusters::Redis::RedisSpecifyShardContextImpl lb_context(
        size, request, Common::Redis::Client::ReadPolicy::Primary, client_zone_);
    Upstream::HostConstSharedPtr host = Upstream::LoadBalancer::onlyAllowSynchronousHostSelection(
        cluster_->loadBalancer().chooseHost(&lb_context));
    if (!host) {
      return size;
    }
    unique_hosts.insert(std::move(host));
  }
  return static_cast<uint16_t>(unique_hosts.size());
}

Common::Redis::Client::PoolRequest*
InstanceImpl::ThreadLocalPool::makeRequest(const std::string& key, RespVariant&& request,
                                           PoolCallbacks& callbacks,
                                           Common::Redis::Client::Transaction& transaction) {
  if (cluster_ == nullptr) {
    ASSERT(client_map_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    return nullptr;
  }

  Clusters::Redis::RedisLoadBalancerContextImpl lb_context(
      key, config_->enableHashtagging(), is_redis_cluster_, getRequest(request),
      transaction.active_ ? Common::Redis::Client::ReadPolicy::Primary : config_->readPolicy(),
      client_zone_);
  Upstream::HostConstSharedPtr host = Upstream::LoadBalancer::onlyAllowSynchronousHostSelection(
      cluster_->loadBalancer().chooseHost(&lb_context));
  if (!host) {
    ENVOY_LOG(debug, "host not found: '{}'", key);
    return nullptr;
  }

  return makeRequestToHost(host, std::move(request), callbacks, transaction);
}

Common::Redis::Client::PoolRequest*
InstanceImpl::ThreadLocalPool::makeRequestToShard(uint16_t shard_index, RespVariant&& request,
                                                  PoolCallbacks& callbacks,
                                                  Common::Redis::Client::Transaction& transaction) {
  if (cluster_ == nullptr) {
    ASSERT(client_map_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    return nullptr;
  }

  Clusters::Redis::RedisSpecifyShardContextImpl lb_context(
      shard_index, getRequest(request),
      transaction.active_ ? Common::Redis::Client::ReadPolicy::Primary : config_->readPolicy(),
      client_zone_);

  Upstream::HostConstSharedPtr host = Upstream::LoadBalancer::onlyAllowSynchronousHostSelection(
      cluster_->loadBalancer().chooseHost(&lb_context));
  if (!host) {
    ENVOY_LOG(debug, "host not found: '{}'", shard_index);
    return nullptr;
  }
  return makeRequestToHost(host, std::move(request), callbacks, transaction);
}

Common::Redis::Client::PoolRequest* InstanceImpl::ThreadLocalPool::makeRequestToHost(
    const std::string& host_address, const Common::Redis::RespValue& request,
    Common::Redis::Client::ClientCallbacks& callbacks) {
  if (cluster_ == nullptr) {
    ASSERT(client_map_.empty());
    ASSERT(host_set_member_update_cb_handle_ == nullptr);
    return nullptr;
  }

  auto colon_pos = host_address.rfind(':');
  if ((colon_pos == std::string::npos) || (colon_pos == (host_address.size() - 1))) {
    return nullptr;
  }

  const std::string ip_address = host_address.substr(0, colon_pos);
  const bool ipv6 = (ip_address.find(':') != std::string::npos);
  std::string host_address_map_key;
  Network::Address::InstanceConstSharedPtr address_ptr;

  if (!ipv6) {
    host_address_map_key = host_address;
  } else {
    const auto ip_port = absl::string_view(host_address).substr(colon_pos + 1);
    uint32_t ip_port_number;
    if (!absl::SimpleAtoi(ip_port, &ip_port_number) || (ip_port_number > 65535)) {
      return nullptr;
    }
    TRY_NEEDS_AUDIT {
      address_ptr = std::make_shared<Network::Address::Ipv6Instance>(ip_address, ip_port_number);
    }
    END_TRY catch (const EnvoyException&) { return nullptr; }
    host_address_map_key = address_ptr->asString();
  }

  auto it = host_address_map_.find(host_address_map_key);
  if (it == host_address_map_.end()) {
    // This host is not known to the cluster manager. Create a new host and insert it into the map.
    if (created_via_redirect_hosts_.size() == config_->maxUpstreamUnknownConnections()) {
      // Too many upstream connections to unknown hosts have been created.
      redis_cluster_stats_.max_upstream_unknown_connections_reached_.inc();
      return nullptr;
    }
    if (!ipv6) {
      // Only create an IPv4 address instance if we need a new Upstream::HostImpl.
      const auto ip_port = absl::string_view(host_address).substr(colon_pos + 1);
      uint32_t ip_port_number;
      if (!absl::SimpleAtoi(ip_port, &ip_port_number) || (ip_port_number > 65535)) {
        return nullptr;
      }
      TRY_NEEDS_AUDIT {
        address_ptr = std::make_shared<Network::Address::Ipv4Instance>(ip_address, ip_port_number);
      }
      END_TRY catch (const EnvoyException&) { return nullptr; }
    }
    Upstream::HostSharedPtr new_host{THROW_OR_RETURN_VALUE(
        Upstream::HostImpl::create(
            cluster_->info(), "", address_ptr, nullptr, nullptr, 1,
            std::make_shared<const envoy::config::core::v3::Locality>(),
            envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
            envoy::config::core::v3::UNKNOWN),
        std::unique_ptr<Upstream::HostImpl>)};
    host_address_map_[host_address_map_key] = new_host;
    created_via_redirect_hosts_.push_back(new_host);
    it = host_address_map_.find(host_address_map_key);
  }

  ThreadLocalActiveClient* client = threadLocalActiveClient(it->second);
  if (client == nullptr) {
    ENVOY_LOG(debug, "redis connection is rate limited");
    return nullptr;
  }

  return client->redis_client_->makeRequest(request, callbacks);
}

Common::Redis::Client::PoolRequest*
InstanceImpl::ThreadLocalPool::makeRequestToHost(Upstream::HostConstSharedPtr& host,
                                                 RespVariant&& request, PoolCallbacks& callbacks,
                                                 Common::Redis::Client::Transaction& transaction) {
  uint32_t client_idx = transaction.current_client_idx_;
  // If there is an active transaction, establish a new connection if necessary.
  if (transaction.active_ && !transaction.connection_established_) {
    ASSERT(cluster_ != nullptr);
    const auto auth_credentials =
        ProtocolOptionsConfigImpl::authCredentials(cluster_->info(), api_, host);

    transaction.clients_[client_idx] =
        client_factory_.create(host, dispatcher_, config_, redis_command_stats_, *(stats_scope_),
                               auth_credentials.username, auth_credentials.password, true,
                               aws_iam_config_, aws_iam_authenticator_, upstream_protocol_version_,
                               makeOptRef(redis_cluster_stats_.upstream_resp3_hello_failure_));
    if (transaction.connection_cb_) {
      transaction.clients_[client_idx]->addConnectionCallbacks(*transaction.connection_cb_);
    }
    // Transaction clients run the same ClientImpl init path as data clients above: RESP3 HELLO
    // 3 negotiation (when configured) happens inside ClientImpl::initialize. MULTI/EXEC and
    // any other commands the splitter dispatches against this client are held by ClientImpl's
    // held-init queue until HELLO (and READONLY when applicable) succeed.
  }

  pending_requests_.emplace_back(*this, std::move(request), callbacks, host);
  PendingRequest& pending_request = pending_requests_.back();

  if (!transaction.active_) {
    ThreadLocalActiveClient* client = this->threadLocalActiveClient(host);
    if (client == nullptr) {
      ENVOY_LOG(debug, "redis connection is rate limited");
      pending_request.request_handler_ = nullptr;
      onRequestCompleted();
      return nullptr;
    }
    pending_request.request_handler_ = client->redis_client_->makeRequest(
        getRequest(pending_request.incoming_request_), pending_request);
  } else {
    pending_request.request_handler_ = transaction.clients_[client_idx]->makeRequest(
        getRequest(pending_request.incoming_request_), pending_request);
  }

  if (pending_request.request_handler_) {
    return &pending_request;
  } else {
    onRequestCompleted();
    return nullptr;
  }
}

void InstanceImpl::ThreadLocalPool::onRequestCompleted() {
  ASSERT(!pending_requests_.empty());

  // The response we got might not be in order, so flush out what we can. (A new response may
  // unlock several out of order responses).
  while (!pending_requests_.empty() && !pending_requests_.front().request_handler_) {
    pending_requests_.pop_front();
  }
}

void InstanceImpl::ThreadLocalActiveClient::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // Is this THE subscription connection for its host? Subscription connections live only in
    // subscription_client_map_ (never client_map_ / clients_to_drain_), so map membership is an
    // exact test.
    auto sub_it = parent_.subscription_client_map_.find(host_);
    const bool was_subscription_connection =
        (sub_it != parent_.subscription_client_map_.end() && sub_it->second.get() == this);

    if (was_subscription_connection) {
      // Two ways a subscription connection closes, one resubscribe driver each (never both — C-6):
      //   * PLANNED removal (onHostsRemoved marked this host): the onClusterTopologyChange that the
      //     member-update callback posts already re-routes the moved channels to their new owner
      //     (and falls back to onUpstreamConnectionClose if that send fails), so DON'T also post
      //     here — that second round is redundant. Consume the mark.
      //   * GENUINE connection loss (no mark): re-issue the carried channels on a fresh connection
      //     against the current topology via a backoff-scheduled onUpstreamConnectionClose.
      // Either way this is the single close→cleanup site (onHostsRemoved does not post), so one
      // close yields one round.
      // The close reason is stamped on this wrapper by onHostsRemoved (A-5: ``planned_removal_``),
      // not reconstructed from a separate per-thread host set. The wrapper is destroyed right after
      // this close, so the flag needs no clearing.
      if (parent_.subscription_registry_) {
        auto registry = parent_.subscription_registry_;
        // Clear THIS dead connection's control ledger (expected SUNSUBSCRIBE acks + outstanding
        // control commands) SYNCHRONOUSLY and UNCONDITIONALLY, before returning to the dispatcher
        // (G7 / A1-3). A replacement subscription connection to the same host can be created — and
        // issue fresh control commands — within this same dispatcher iteration (e.g. a downstream
        // SUBSCRIBE for a channel that hashes here); a deferred clear would wipe that fresh ledger
        // and orphan its in-order error correlation. Clearing even when the registry is momentarily
        // empty (or the host is being removed) matters: a dead connection that left FIFO entries
        // behind would otherwise head-mismatch the host's NEXT subscribe epoch's ack (a 10s hang
        // and an innocent downstream close). The clear is an idempotent map erase, so it is
        // unconditional — the ``!empty()`` / ``planned_removal_`` gate belongs only on the
        // resubscribe half.
        registry->forgetHostConnectionLedger(host_);
        // Skip the resubscribe entirely on a PLANNED removal (the onClusterTopologyChange the
        // member-update callback posts already re-routes the moved channels — C-6) or an empty
        // registry (nothing to re-issue).
        if (!planned_removal_ && !registry->empty()) {
          // Seed the retry scope SYNCHRONOUSLY (S6-5), scoped to THIS host's channels (F3): capture
          // exactly the channels this dead connection carried BEFORE a same-iteration replacement
          // subscribe to the same host can add a fresh channel — which must NOT be re-SSUBSCRIBEd.
          // It keeps each channel's owner (D1), so a concurrent unsubscribe in the window still
          // sends its SUNSUBSCRIBE and the dedup path still defers a joiner.
          registry->markHostChannelsForResubscribe(host_);
          // Defer only the pool-touching backoff arm, weak-captured (MISC-3) — see postToRegistry.
          parent_.postToRegistry(registry,
                                 [](SubscriptionRegistry& reg) { reg.armResubscribeBackoff(); });
        }
      }
      parent_.dispatcher_.deferredDelete(std::move(redis_client_));
      parent_.subscription_client_map_.erase(sub_it);
      return;
    }

    // Identity check (mirrors the subscription branch above): only tear down the host's DATA client
    // if THIS wrapper is it. Without this, a subscription connection being retired — whose entry
    // maybeCleanupSubscriptionMode already erased from subscription_client_map_ before close()
    // drove this onEvent — falls through here and erases/destroys the host's healthy, in-flight
    // DATA client (release: lost request callbacks + a dangling PoolRequest* → UAF on a later
    // cancel; debug: ~ClientImpl ASSERT abort). The retire path deferred-deletes its own client, so
    // this onEvent is correctly a no-op for a subscription connection.
    auto client_to_delete = parent_.client_map_.find(host_);
    if (client_to_delete != parent_.client_map_.end() && client_to_delete->second.get() == this) {
      parent_.dispatcher_.deferredDelete(std::move(redis_client_));
      parent_.client_map_.erase(client_to_delete);
    } else {
      for (auto it = parent_.clients_to_drain_.begin(); it != parent_.clients_to_drain_.end();
           it++) {
        if ((*it).get() == this) {
          if (!redis_client_->active()) {
            parent_.redis_cluster_stats_.upstream_cx_drained_.inc();
          }
          parent_.dispatcher_.deferredDelete(std::move(redis_client_));
          parent_.clients_to_drain_.erase(it);
          break;
        }
      }
    }
  }
}

InstanceImpl::PendingRequest::PendingRequest(InstanceImpl::ThreadLocalPool& parent,
                                             RespVariant&& incoming_request,
                                             PoolCallbacks& pool_callbacks,
                                             Upstream::HostConstSharedPtr& host)
    : parent_(parent), incoming_request_(std::move(incoming_request)),
      pool_callbacks_(pool_callbacks), host_(host) {}

InstanceImpl::PendingRequest::~PendingRequest() {
  cache_load_handle_.reset();

  if (request_handler_) {
    request_handler_->cancel();
    request_handler_ = nullptr;
    // If we have to cancel the request on the client, then we'll treat this as failure for pool
    // callback
    pool_callbacks_.onFailure();
  }
}

void InstanceImpl::PendingRequest::onResponse(Common::Redis::RespValuePtr&& response) {
  request_handler_ = nullptr;
  pool_callbacks_.onResponse(std::move(response));
  parent_.onRequestCompleted();
}

void InstanceImpl::PendingRequest::onFailure() {
  request_handler_ = nullptr;
  pool_callbacks_.onFailure();
  parent_.refresh_manager_->onFailure(parent_.cluster_name_);
  parent_.onRequestCompleted();
}

void InstanceImpl::PendingRequest::onRedirection(Common::Redis::RespValuePtr&& value,
                                                 const std::string& host_address,
                                                 bool ask_redirection) {
  if (!parent_.dns_cache_) {
    doRedirection(std::move(value), host_address, ask_redirection);
    return;
  }

  resp_value_ = std::move(value);
  ask_redirection_ = ask_redirection;
  auto result = parent_.dns_cache_->loadDnsCacheEntry(host_address, default_port, false, *this);
  cache_load_handle_ = std::move(result.handle_);

  switch (result.status_) {
  case Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::InCache: {
    ASSERT(cache_load_handle_ == nullptr);
    if (!result.host_info_.has_value() || !result.host_info_.value()->address()) {
      ENVOY_LOG(debug, "DNS entry for '{}' was in cache but did not contain an address",
                host_address);
      auto host = host_;
      onResponse(std::move(resp_value_));
      host->cluster().trafficStats()->upstream_internal_redirect_failed_total_.inc();
    } else {
      doRedirection(std::move(resp_value_),
                    formatAddress(*result.host_info_.value()->address()->ip()), ask_redirection_);
    }
    return;
  }
  case Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading:
    ASSERT(cache_load_handle_ != nullptr);
    // The client-side PendingRequest was already popped before this callback fired
    // (ClientImpl::onRespValue pops BEFORE dispatching), so the PoolRequest* held in
    // ``request_handler_`` now dangles. Clear it: there is nothing to cancel on the upstream during
    // the async DNS window — ``cache_load_handle_`` is the live handle (reset by the dtor /
    // onLoadDnsCacheComplete), and doRedirection installs a fresh ``request_handler_`` once the
    // lookup lands. Leaving the stale pointer would UAF on a concurrent cancel() or ~PendingRequest
    // (C-1).
    request_handler_ = nullptr;
    return;
  case Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Overflow:
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_LOG(debug, "DNS lookup for '{}' was not performed due to an overflow in the cache",
              host_address);
    auto host = host_;
    onResponse(std::move(resp_value_));
    host->cluster().trafficStats()->upstream_internal_redirect_failed_total_.inc();
    return;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

std::string InstanceImpl::PendingRequest::formatAddress(const Envoy::Network::Address::Ip& ip) {
  return fmt::format("{}:{}", ip.addressAsString(), ip.port());
}
void InstanceImpl::PendingRequest::onLoadDnsCacheComplete(
    const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  cache_load_handle_.reset();

  if (!host_info || !host_info->address()) {
    ENVOY_LOG(debug, "DNS lookup failed");
    auto host = host_;
    onResponse(std::move(resp_value_));
    host->cluster().trafficStats()->upstream_internal_redirect_failed_total_.inc();
  } else {
    doRedirection(std::move(resp_value_), formatAddress(*host_info->address()->ip()),
                  ask_redirection_);
  }
}

void InstanceImpl::PendingRequest::doRedirection(Common::Redis::RespValuePtr&& value,
                                                 const std::string& host_address,
                                                 bool ask_redirection) {
  // This request might go away, so keep a copy of host.
  auto host = host_;

  // Prepend request with an asking command if redirected via an ASK error. The returned handle is
  // not important since there is no point in being able to cancel the request. The use of
  // null_pool_callbacks ensures the transparent filtering of the Redis server's response to the
  // "asking" command; this is fine since the server either responds with an OK or an error message
  // if cluster support is not enabled (in which case we should not get an ASK redirection error).
  if (ask_redirection &&
      !parent_.makeRequestToHost(host_address, Common::Redis::Utility::AskingRequest::instance(),
                                 null_client_callbacks)) {
    onResponse(std::move(value));
    host->cluster().trafficStats()->upstream_internal_redirect_failed_total_.inc();
  } else {
    request_handler_ =
        parent_.makeRequestToHost(host_address, getRequest(incoming_request_), *this);
    if (!request_handler_) {
      onResponse(std::move(value));
      host->cluster().trafficStats()->upstream_internal_redirect_failed_total_.inc();
    } else {
      parent_.refresh_manager_->onRedirection(parent_.cluster_name_);
      host->cluster().trafficStats()->upstream_internal_redirect_succeeded_total_.inc();
    }
  }
}

void InstanceImpl::PendingRequest::cancel() {
  // ``request_handler_`` is null during an in-flight DNS-redirect (Loading) window — the
  // client-side request was already popped, so there is nothing to cancel upstream (C-1); the
  // dtor's own
  // ``if (request_handler_)`` guard relies on the same Loading-branch clear.
  if (request_handler_ != nullptr) {
    request_handler_->cancel();
    request_handler_ = nullptr;
  }
  parent_.onRequestCompleted();
}

SubscriptionRegistryPtr InstanceImpl::subscriptionRegistryShared() {
  auto& tls_pool = tls_->getTyped<ThreadLocalPool>();
  // Pub/sub via Push frames is RESP3-only. Without the listener opting into RESP3 there is no
  // path for Push messages, so return nullptr to disable subscription handling on this filter.
  if (tls_pool.upstream_protocol_version_ != Common::Redis::RespProtocolVersion::Resp3) {
    return nullptr;
  }
  if (!tls_pool.subscription_registry_) {
    // Thread the pub/sub tuning knobs from ConnPoolSettings.pubsub_settings (A-7); each getter
    // returns the historical default when the config omits the field.
    tls_pool.subscription_registry_ = std::make_shared<RedisProxy::SubscriptionRegistry>(
        tls_pool, tls_pool.api_.randomGenerator(), tls_pool.dispatcher_,
        tls_pool.config_->subscribeAckTimeout(), tls_pool.config_->resubscribeBackoffBaseInterval(),
        tls_pool.config_->resubscribeBackoffMaxInterval());
  }
  return tls_pool.subscription_registry_;
}

InstanceImpl::ThreadLocalActiveClient* InstanceImpl::ThreadLocalPool::getOrCreateSubscriptionClient(
    Upstream::HostConstSharedPtr host,
    Common::Redis::Client::PushMessageCallbacks& push_callbacks) {
  // Dedicated subscription connection (subscription_client_map_), never shared with data
  // requests — so an in-band reply here is unambiguously a failed subscribe.
  ThreadLocalActiveClient* client = threadLocalActiveSubscriptionClient(host);
  if (client == nullptr) {
    ENVOY_LOG(debug, "redis: failed to get client for subscription");
    return nullptr;
  }
  // ClientImpl::initialize already drove HELLO 3 on this connection (the cluster is RESP3 —
  // subscriptionRegistryShared above gated on that). Subsequent sendCommand calls for
  // ``SUBSCRIBE``/``PSUBSCRIBE``/``SSUBSCRIBE`` go through ClientImpl::sendCommand, which holds the
  // command behind the held-init queue if HELLO is still in flight and replays it in
  // submission order on Ready. No manual HELLO dispatch or per-host tracking needed here.
  client->redis_client_->setPushCallbacks(&push_callbacks);
  return client;
}

Upstream::HostConstSharedPtr
InstanceImpl::ThreadLocalPool::chooseUpstreamHostForChannel(const std::string& channel) {
  if (cluster_ == nullptr) {
    return nullptr;
  }
  Common::Redis::RespValue dummy_request;
  // Hashtagging MUST match the PUBLISH/SPUBLISH path (see PublishRequest, which uses
  // ``config_->enableHashtagging()``): if subscribe and publish hash different substrings of a
  // ``{tag}key`` channel, they select different shards on a non-Redis-cluster upstream and messages
  // are silently lost. ``is_redis_cluster_`` upstreams force hashtagging on both sides regardless.
  //
  // This resolves to the PRIMARY replica, ALWAYS — and that is intentional, not an oversight
  // (A2-3). ``dummy_request`` is an empty RespValue, so RedisLoadBalancerContextImpl's
  // isReadRequest sees a null command and ``is_read_`` is false; the LB then ignores the
  // ``readPolicy()`` /
  // ``client_zone_`` arguments and picks the primary. The two arguments are passed only to keep
  // this call shape identical to the data path's makeRequest — they are deliberately moot here. Do
  // NOT "fix" this into zone-/replica-aware routing by handing it a read-shaped request:
  // primary-only resolution is load-bearing. onClusterTopologyChange dry-resolves each channel here
  // to decide "did the owner move?", so a replica/zone-varying result would judge most channels as
  // "moved" on every topology event and churn re-subscribes; and SPUBLISH routes to the primary, so
  // a subscribe that landed on a replica would sit on a different host than the messages it must
  // receive.
  Clusters::Redis::RedisLoadBalancerContextImpl lb_context(channel, config_->enableHashtagging(),
                                                           is_redis_cluster_, dummy_request,
                                                           config_->readPolicy(), client_zone_);
  return Upstream::LoadBalancer::onlyAllowSynchronousHostSelection(
      cluster_->loadBalancer().chooseHost(&lb_context));
}

bool InstanceImpl::ThreadLocalPool::sendUpstreamSsubscribeToHost(
    const std::string& channel, Common::Redis::Client::PushMessageCallbacks& push_callbacks,
    const Upstream::HostConstSharedPtr& host) {
  if (!host) {
    return false;
  }
  ThreadLocalActiveClient* client = getOrCreateSubscriptionClient(host, push_callbacks);
  if (client == nullptr) {
    // Rate-limited (or no client available). getOrCreateClientInMap already erased the null
    // placeholder it inserted for the lookup, so subscription_client_map_ carries no stale null
    // entry for ~ThreadLocalPool / onClusterRemoval / onHostsRemoved to dereference.
    return false;
  }

  Common::Redis::RespValue cmd = Common::Redis::Utility::makeRequest("SSUBSCRIBE", {channel});
  client->redis_client_->sendCommand(cmd);
  return true;
}

Upstream::HostConstSharedPtr InstanceImpl::ThreadLocalPool::sendUpstreamSsubscribe(
    const std::string& channel, Common::Redis::Client::PushMessageCallbacks& push_callbacks) {
  Upstream::HostConstSharedPtr host = chooseUpstreamHostForChannel(channel);
  if (!host) {
    ENVOY_LOG(debug, "redis: no upstream host available for ``SSUBSCRIBE`` '{}'", channel);
    return nullptr;
  }
  if (!sendUpstreamSsubscribeToHost(channel, push_callbacks, host)) {
    return nullptr;
  }
  return host;
}

UpstreamSubscriptionCallbacks::SunsubscribeResult
InstanceImpl::ThreadLocalPool::sendUpstreamSunsubscribe(const std::string& channel,
                                                        Upstream::HostConstSharedPtr host) {
  if (!host) {
    return UpstreamSubscriptionCallbacks::SunsubscribeResult::NotSent;
  }

  // FIND-ONLY (F2): never CREATE a subscription connection just to send a SUNSUBSCRIBE. A missing
  // entry means the host's subscription connection is already gone — and with it the upstream
  // subscription state — so there is nothing to unsubscribe. Creating one here (as get-or-create
  // did) makes a fresh, callback-less connection: push_callbacks_ is installed only by
  // getOrCreateSubscriptionClient, so ClientImpl::onRespValue would DROP the SUNSUBSCRIBE ack Push,
  // permanently polluting the host's control FIFO head and blacking out its later ack correlation.
  auto it = subscription_client_map_.find(host);
  if (it == subscription_client_map_.end()) {
    return UpstreamSubscriptionCallbacks::SunsubscribeResult::NotSent;
  }
  // subscription_client_map_ never holds a null placeholder (ALT-3): getOrCreateClientInMap erases
  // the operator[]-inserted slot on the rate-limited path and otherwise fills it with a real
  // client, so a present entry is always a live client. Assert the invariant rather than "repair"
  // an impossible null here while retireSubscriptionConnectionIfIdle silently preserves it — the
  // two contradictory defenses were the ALT-3 smell.
  ASSERT(it->second != nullptr);
  ThreadLocalActiveClientPtr& client = it->second;

  Common::Redis::RespValue cmd = Common::Redis::Utility::makeRequest("SUNSUBSCRIBE", {channel});
  client->redis_client_->sendCommand(cmd);
  // If this was the host's last channel, the connection is retired inline here and will never ack
  // the SUNSUBSCRIBE. Report that so the registry drops its expected-ack bookkeeping for the host
  // instead of leaving a stale entry (see SubscriptionRegistry::sendSunsubscribe). Hand off the
  // iterator we already resolved above so the retire does not look the host up a second time (§6);
  // ``client``/``it`` are not used after this call, and the retire only ever erases this very
  // entry.
  const bool retired = maybeCleanupSubscriptionMode(host, it);
  return retired ? UpstreamSubscriptionCallbacks::SunsubscribeResult::ConnectionRetired
                 : UpstreamSubscriptionCallbacks::SunsubscribeResult::AckExpected;
}

void InstanceImpl::ThreadLocalPool::postToRegistry(const SubscriptionRegistryPtr& registry,
                                                   std::function<void(SubscriptionRegistry&)> fn) {
  std::weak_ptr<SubscriptionRegistry> weak_registry = registry;
  dispatcher_.post([weak_registry, fn = std::move(fn)]() {
    if (auto reg = weak_registry.lock()) {
      fn(*reg);
    }
  });
}

void InstanceImpl::ThreadLocalPool::scheduleResubscribe(std::chrono::milliseconds delay) {
  // The timer callback reads subscription_registry_ at fire time. Teardown paths
  // (onClusterRemoval and ~ThreadLocalPool) reset subscription_registry_ AND disable this timer
  // together, so the callback can never run against a destroyed registry — no stored callback or
  // strong-ref is needed to keep it alive. The timer is a member (Event::TimerPtr): destroying
  // the pool destroys it, cancelling any pending fire.
  if (!resubscribe_timer_) {
    resubscribe_timer_ = dispatcher_.createTimer([this]() {
      if (subscription_registry_) {
        subscription_registry_->doResubscribe();
      }
    });
  }
  resubscribe_timer_->enableTimer(delay);
}

void InstanceImpl::ThreadLocalPool::requestTopologyRefresh() {
  // Route through the shared cluster refresh manager, exactly as the data path does on a MOVED/ASK
  // redirection (see PendingRequest::doRedirection). The manager throttles refreshes, so a burst of
  // subscription redirects during a reshard collapses into a bounded number of slot-map refreshes.
  if (refresh_manager_ != nullptr) {
    refresh_manager_->onRedirection(cluster_name_);
  }
}

bool InstanceImpl::ThreadLocalPool::retireSubscriptionConnectionIfIdle(
    const Upstream::HostConstSharedPtr& host) {
  if (!host) {
    return false;
  }
  auto it = subscription_client_map_.find(host);
  if (it == subscription_client_map_.end()) {
    return false;
  }
  // No null-placeholder entry can survive in subscription_client_map_ (ALT-3, see
  // sendUpstreamSunsubscribe); maybeCleanupSubscriptionMode re-asserts this below.
  ASSERT(it->second != nullptr);
  // maybeCleanupSubscriptionMode retires the connection only if the registry now reports this host
  // has no remaining subscriptions (hostHasSubscriptions) — which the registry made true by
  // forgetting the invalidated channel's mapping before calling us. Reuse the entry we just
  // resolved (§6); ``it`` is unused after this call. Return its decision so the registry caller
  // clears the host's control ledger off the ACTUAL retire rather than re-deriving the predicate
  // (ALT-1).
  return maybeCleanupSubscriptionMode(host, it);
}

bool InstanceImpl::ThreadLocalPool::maybeCleanupSubscriptionMode(
    const Upstream::HostConstSharedPtr& host, SubscriptionClientMap::iterator client_it) {
  if (!subscription_registry_) {
    return false;
  }
  // Does this host still carry any subscription? O(1) via the registry's per-host channel index
  // (this was a full linear scan of the channel→host map on every SUNSUBSCRIBE, so tearing down a
  // subscriber on N channels of one host was O(n^2)). If so, the connection stays open and will
  // ack. This query reads the registry, never subscription_client_map_, so ``client_it`` stays
  // valid across it.
  if (subscription_registry_->hostHasSubscriptions(host)) {
    return false;
  }

  // No subscriptions left on this host — retire its now-idle dedicated subscription connection.
  // Remove it from subscription_client_map_ FIRST so the close-driven onEvent does not treat this
  // as an involuntary loss and post a resubscribe: these subscriptions were dropped by the client,
  // not lost to a failure. After the erase onEvent finds the connection in no map and no-ops, so
  // we defer-delete the ClientImpl ourselves (the ThreadLocalActiveClient wrapper is destroyed as
  // the local unique_ptr unwinds — the same immediate-wrapper / deferred-client split onEvent
  // uses on the data path). Both callers guarantee a live entry, so no re-lookup or null guard is
  // needed here (§6).
  ASSERT(client_it != subscription_client_map_.end() && client_it->second != nullptr);
  ThreadLocalActiveClientPtr retired = std::move(client_it->second);
  subscription_client_map_.erase(client_it);
  retired->redis_client_->close();
  dispatcher_.deferredDelete(std::move(retired->redis_client_));
  // The host had no remaining subscriptions, so its connection is retired and will not ack the
  // SUNSUBSCRIBE just sent. Return true so the registry caller (sendSunsubscribe via
  // ConnectionRetired, or retireSubscriptionConnectionIfIdle via this return) drops the host's
  // control ledger off this ACTUAL retire decision rather than re-deriving the predicate (ALT-1).
  return true;
}

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
