#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.h"
#include "envoy/extensions/clusters/redis/v3/redis_cluster.pb.validate.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/common/network/utility.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include "test/extensions/clusters/redis/mocks.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/extensions/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/extensions/filters/network/redis_proxy/pubsub_test_utils.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/cluster_update_callbacks_handle.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/thread_local_cluster.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {

class RedisConnPoolImplTest : public testing::Test, public Common::Redis::Client::ClientFactory {
public:
  void setup(bool cluster_exists = true, bool hashtagging = true, uint32_t max_unknown_conns = 100,
             const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache = nullptr,
             uint32_t redis_cx_rate_limit_per_sec = 100,
             Common::Redis::RespProtocolVersion protocol_version =
                 Common::Redis::RespProtocolVersion::Resp2) {
    EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (cluster_exists) {
      cm_.initializeThreadLocalClusters({"fake_cluster"});
    }

    upstream_cx_drained_.value_ = 0;
    ON_CALL(store_, counter(Eq("upstream_cx_drained")))
        .WillByDefault(ReturnRef(upstream_cx_drained_));
    ON_CALL(upstream_cx_drained_, value()).WillByDefault(Invoke([&]() -> uint64_t {
      return upstream_cx_drained_.value_;
    }));
    ON_CALL(upstream_cx_drained_, inc()).WillByDefault(Invoke([&]() {
      upstream_cx_drained_.value_++;
    }));

    max_upstream_unknown_connections_reached_.value_ = 0;
    ON_CALL(store_, counter(Eq("max_upstream_unknown_connections_reached")))
        .WillByDefault(ReturnRef(max_upstream_unknown_connections_reached_));
    ON_CALL(max_upstream_unknown_connections_reached_, value())
        .WillByDefault(
            Invoke([&]() -> uint64_t { return max_upstream_unknown_connections_reached_.value_; }));
    ON_CALL(max_upstream_unknown_connections_reached_, inc()).WillByDefault(Invoke([&]() {
      max_upstream_unknown_connections_reached_.value_++;
    }));

    connection_rate_limited_.value_ = 0;
    ON_CALL(store_, counter(Eq("connection_rate_limited")))
        .WillByDefault(ReturnRef(connection_rate_limited_));
    ON_CALL(connection_rate_limited_, value()).WillByDefault(Invoke([&]() -> uint64_t {
      return connection_rate_limited_.value_;
    }));
    ON_CALL(connection_rate_limited_, inc()).WillByDefault(Invoke([&]() {
      connection_rate_limited_.value_++;
    }));

    cluster_refresh_manager_ =
        std::make_shared<NiceMock<Extensions::Common::Redis::MockClusterRefreshManager>>();
    auto redis_command_stats =
        Common::Redis::RedisCommandStats::createRedisCommandStats(store_.symbolTable());
    std::shared_ptr<InstanceImpl> conn_pool_impl = std::make_shared<InstanceImpl>(
        cluster_name_, cm_, *this, tls_,
        Common::Redis::Client::createConnPoolSettings(20, hashtagging, true, max_unknown_conns,
                                                      read_policy_, redis_cx_rate_limit_per_sec),
        api_, store_.rootScope(), redis_command_stats, cluster_refresh_manager_, dns_cache,
        std::nullopt, std::nullopt, /*local_zone=*/"", protocol_version);
    conn_pool_impl->init();
    // Set the authentication password for this connection pool.
    conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().auth_username_ = auth_username_;
    conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().auth_password_ = auth_password_;
    conn_pool_ = std::move(conn_pool_impl);
    test_address_ = *Network::Utility::resolveUrl("tcp://127.0.0.1:3000");
  }

  void makeSimpleRequest(bool create_client, const std::string& hash_key, uint64_t hash_value) {
    auto expectHash = [&](const uint64_t hash) {
      return [&, hash](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), hash);
        return cm_.thread_local_cluster_.lb_.host_;
      };
    };

    EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
        .WillOnce(Invoke(expectHash(hash_value)));
    if (create_client) {
      client_ = new NiceMock<Common::Redis::Client::MockClient>();
      EXPECT_CALL(*this, create_(_)).WillOnce(Return(client_));
    }
    Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
    MockPoolCallbacks callbacks;
    std::list<Common::Redis::Client::ClientCallbacks*> client_callbacks;
    Common::Redis::Client::MockPoolRequest active_request;
    EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(test_address_));
    EXPECT_CALL(*client_, makeRequest_(Ref(*value), _))
        .WillOnce(Invoke(
            [&](const Common::Redis::RespValue&, Common::Redis::Client::ClientCallbacks& callbacks)
                -> Common::Redis::Client::PoolRequest* {
              client_callbacks.push_back(&callbacks);
              return &active_request;
            }));
    Common::Redis::Client::PoolRequest* request =
        conn_pool_->makeRequest(hash_key, value, callbacks, transaction_);
    EXPECT_NE(nullptr, request);
    EXPECT_NE(nullptr, client_callbacks.back());

    EXPECT_CALL(active_request, cancel());
    request->cancel();
  }

  void makeRequest(Common::Redis::Client::MockClient* client,
                   Common::Redis::RespValueSharedPtr& value, MockPoolCallbacks& callbacks,
                   Common::Redis::Client::MockPoolRequest& active_request,
                   bool create_client = true) {
    EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
        .WillOnce(
            Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
              EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
              EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
              EXPECT_EQ(context->downstreamConnection(), nullptr);
              return this->cm_.thread_local_cluster_.lb_.host_;
            }));
    if (create_client) {
      EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
    }
    EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(this->test_address_));
    EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
    Common::Redis::Client::PoolRequest* request =
        this->conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
    EXPECT_NE(nullptr, request);
  }

  std::string getAuthUsername() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().auth_username_;
  }

  std::string getAuthPassword() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().auth_password_;
  }

  absl::node_hash_map<Upstream::HostConstSharedPtr, InstanceImpl::ThreadLocalActiveClientPtr>&
  clientMap() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().client_map_;
  }

  InstanceImpl::ThreadLocalActiveClient* clientMap(Upstream::HostConstSharedPtr host) {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().client_map_[host].get();
  }

  absl::node_hash_map<std::string, Upstream::HostConstSharedPtr>& hostAddressMap() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().host_address_map_;
  }

  std::list<Upstream::HostSharedPtr>& createdViaRedirectHosts() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>()
        .created_via_redirect_hosts_;
  }

  std::list<InstanceImpl::ThreadLocalActiveClientPtr>& clientsToDrain() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().clients_to_drain_;
  }

  InstanceImpl::ThreadLocalPool& threadLocalPool() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>();
  }

  // TEST_F bodies are fixture SUBCLASSES and do not inherit its friendship, so private state is
  // read through fixture methods like this one.
  size_t pendingRequestsSize() { return threadLocalPool().pending_requests_.size(); }

  Event::TimerPtr& drainTimer() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().drain_timer_;
  }

  void drainClients() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().drainClients();
  }

  Stats::Counter& upstreamCxDrained() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->redis_cluster_stats_.upstream_cx_drained_;
  }

  Stats::Counter& maxUpstreamUnknownConnectionsReached() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->redis_cluster_stats_.max_upstream_unknown_connections_reached_;
  }

  Stats::Counter& connectionRateLimited() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->redis_cluster_stats_.connection_rate_limited_;
  }

  Stats::Counter& upstreamResp3HelloFailure() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->redis_cluster_stats_.upstream_resp3_hello_failure_;
  }

  // Common::Redis::Client::ClientFactory
  Common::Redis::Client::ClientPtr
  create(Upstream::HostConstSharedPtr host, Event::Dispatcher&,
         const Common::Redis::Client::ConfigSharedPtr&,
         const Common::Redis::RedisCommandStatsSharedPtr&, Stats::Scope&,
         const std::string& username, const std::string& password, bool is_transaction_client,
         std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam>,
         std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>,
         Common::Redis::RespProtocolVersion upstream_protocol_version,
         OptRef<Stats::Counter> upstream_resp3_hello_failure) override {
    EXPECT_EQ(auth_username_, username);
    EXPECT_EQ(auth_password_, password);
    last_upstream_protocol_version_ = upstream_protocol_version;
    last_upstream_resp3_hello_failure_ = upstream_resp3_hello_failure.ptr();
    last_is_transaction_client_ = is_transaction_client;
    return Common::Redis::Client::ClientPtr{create_(host)};
  }

  // Captured args from the most recent create() call — used by the plumbing-pin tests below
  // to assert the conn pool forwards the right values to the client factory.
  Common::Redis::RespProtocolVersion last_upstream_protocol_version_{
      Common::Redis::RespProtocolVersion::Resp2};
  bool last_is_transaction_client_{false};
  Stats::Counter* last_upstream_resp3_hello_failure_{nullptr};

  void testReadPolicy(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::ReadPolicy
          read_policy,
      NetworkFilters::Common::Redis::Client::ReadPolicy expected_read_policy) {
    InSequence s;

    read_policy_ = read_policy;
    setup();

    Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
    Common::Redis::Client::MockPoolRequest auth_request, active_request, readonly_request;
    MockPoolCallbacks callbacks;
    Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

    EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
        .WillOnce(
            Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
              EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
              EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
              EXPECT_EQ(context->downstreamConnection(), nullptr);
              auto redis_context =
                  dynamic_cast<Clusters::Redis::RedisLoadBalancerContext*>(context);
              EXPECT_EQ(redis_context->readPolicy(), expected_read_policy);
              return {cm_.thread_local_cluster_.lb_.host_};
            }));
    EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
    EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(test_address_));
    EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
    Common::Redis::Client::PoolRequest* request =
        conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
    EXPECT_NE(nullptr, request);

    EXPECT_CALL(active_request, cancel());
    EXPECT_CALL(callbacks, onFailure_());
    EXPECT_CALL(*client, close());
    tls_.shutdownThread();
  }

  void respond(MockPoolCallbacks& callbacks, Common::Redis::Client::MockClient* client) {
    EXPECT_CALL(callbacks, onResponse_(_));
    client->client_callbacks_.back()->onResponse(std::make_unique<Common::Redis::RespValue>());
    EXPECT_EQ(0,
              conn_pool_->tls_->getTyped<InstanceImpl::ThreadLocalPool>().pending_requests_.size());
  }

  void verifyInvalidMoveResponse(Common::Redis::Client::MockClient* client,
                                 const std::string& host_address, bool create_client) {
    Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
    Common::Redis::Client::MockPoolRequest active_request;
    MockPoolCallbacks callbacks;
    makeRequest(client, request_value, callbacks, active_request, create_client);
    Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
    moved_response->type(Common::Redis::RespType::Error);
    moved_response->asString() = "MOVE 1111 " + host_address;
    EXPECT_CALL(callbacks, onResponse_(Ref(moved_response)));
    EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
    const auto expected = cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                              .counter("upstream_internal_redirect_failed_total")
                              .value() +
                          1;
    client->client_callbacks_.back()->onRedirection(std::move(moved_response), host_address, false);
    EXPECT_EQ(expected, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                            .counter("upstream_internal_redirect_failed_total")
                            .value());
  }

  MOCK_METHOD(Common::Redis::Client::Client*, create_, (Upstream::HostConstSharedPtr host));

  NiceMock<Stats::MockStore> store_;
  const std::string cluster_name_{"fake_cluster"};
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  std::shared_ptr<InstanceImpl> conn_pool_;
  Upstream::ClusterUpdateCallbacks* update_callbacks_{};
  Common::Redis::Client::MockClient* client_{};
  Network::Address::InstanceConstSharedPtr test_address_;
  std::string auth_username_;
  std::string auth_password_;
  NiceMock<Api::MockApi> api_;
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::ReadPolicy
      read_policy_ = envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
          ConnPoolSettings::MASTER;
  NiceMock<Stats::MockCounter> upstream_cx_drained_;
  NiceMock<Stats::MockCounter> max_upstream_unknown_connections_reached_;
  NiceMock<Stats::MockCounter> connection_rate_limited_;
  std::shared_ptr<NiceMock<Extensions::Common::Redis::MockClusterRefreshManager>>
      cluster_refresh_manager_;
  Common::Redis::Client::NoOpTransaction transaction_;
};

TEST_F(RedisConnPoolImplTest, Basic) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return {cm_.thread_local_cluster_.lb_.host_};
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_NE(nullptr, request);

  EXPECT_CALL(active_request, cancel());
  EXPECT_CALL(callbacks, onFailure_());
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, ShardSize) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  uint16_t shard_size = 3;
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillRepeatedly(
          Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
            EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
            EXPECT_EQ(context->downstreamConnection(), nullptr);
            std::cout << (context->computeHashKey().value()) << std::endl;
            if (context->computeHashKey() < shard_size) {
              return cm_.thread_local_cluster_.lb_.host_;
            }
            return nullptr;
          }));
  EXPECT_CALL(*this, create_(_)).WillRepeatedly(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_EQ(conn_pool_->shardSize(), shard_size);

  for (uint16_t i = 0; i < 100; i++) {
    shard_size = i;
    EXPECT_EQ(conn_pool_->shardSize(), shard_size);
  }

  delete client;
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, ShardHost) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), 0);
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return cm_.thread_local_cluster_.lb_.host_;
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequestToShard(0, value, callbacks, transaction_);
  EXPECT_NE(nullptr, request);

  EXPECT_CALL(active_request, cancel());
  EXPECT_CALL(callbacks, onFailure_());
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, ShardNoHost) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), 0);
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return nullptr;
      }));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequestToShard(0, value, callbacks, transaction_);
  EXPECT_EQ(nullptr, request);

  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, ShardSizeDuplicateHosts) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;

  uint16_t max_hosts = 5;
  std::vector<std::shared_ptr<NiceMock<Upstream::MockHost>>> mock_hosts;
  for (uint16_t i = 0; i < max_hosts; i++) {
    mock_hosts.push_back(std::make_shared<NiceMock<Upstream::MockHost>>());
  }

  uint16_t call_count = 0;
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillRepeatedly(
          Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
            EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
            EXPECT_EQ(context->downstreamConnection(), nullptr);
            return mock_hosts[call_count++ % max_hosts];
          }));
  // shardSize() should only count max_hosts, ignoring duplicates
  EXPECT_EQ(conn_pool_->shardSize(), max_hosts);
  EXPECT_GT(call_count, max_hosts);

  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, BasicRespVariant) {
  InSequence s;

  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return {cm_.thread_local_cluster_.lb_.host_};
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Eq(value), _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", ConnPool::RespVariant(value), callbacks, transaction_);
  EXPECT_NE(nullptr, request);

  EXPECT_CALL(active_request, cancel());
  EXPECT_CALL(callbacks, onFailure_());
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, ShardRequestFailed) {
  InSequence s;

  setup();

  Common::Redis::RespValue value;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), 0);
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return cm_.thread_local_cluster_.lb_.host_;
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Eq(value), _)).WillOnce(Return(nullptr));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequestToShard(0, ConnPool::RespVariant(value), callbacks, transaction_);

  // the request should be null and the callback is not called
  EXPECT_EQ(nullptr, request);
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, ClientRequestFailed) {
  InSequence s;

  setup();

  Common::Redis::RespValue value;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return {cm_.thread_local_cluster_.lb_.host_};
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Eq(value), _)).WillOnce(Return(nullptr));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", ConnPool::RespVariant(value), callbacks, transaction_);

  // the request should be null and the callback is not called
  EXPECT_EQ(nullptr, request);
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, RedisConnectionRateLimited) {
  InSequence s;

  setup(true, true, 100, nullptr, 1);

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return {cm_.thread_local_cluster_.lb_.host_};
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Eq(value), _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", ConnPool::RespVariant(value), callbacks, transaction_);
  EXPECT_NE(nullptr, request);
  EXPECT_EQ(connectionRateLimited().value(), 0);

  // close local and reconnect, should be rate limited and get null
  client->raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return {cm_.thread_local_cluster_.lb_.host_};
      }));
  EXPECT_CALL(*this, create_(_)).Times(0);
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  Common::Redis::Client::PoolRequest* rate_limited_request =
      conn_pool_->makeRequest("hash_key", ConnPool::RespVariant(value), callbacks, transaction_);
  EXPECT_EQ(nullptr, rate_limited_request);
  EXPECT_EQ(connectionRateLimited().value(), 1);

  // wait for a second and rate limiter should recover
  tls_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(1));
  Common::Redis::Client::MockPoolRequest new_active_request;
  MockPoolCallbacks new_callbacks;
  Common::Redis::Client::MockClient* new_client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return {cm_.thread_local_cluster_.lb_.host_};
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(new_client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*new_client, makeRequest_(Eq(value), _)).WillOnce(Return(&new_active_request));
  Common::Redis::Client::PoolRequest* new_request = conn_pool_->makeRequest(
      "hash_key", ConnPool::RespVariant(value), new_callbacks, transaction_);
  EXPECT_NE(nullptr, new_request);
  EXPECT_EQ(connectionRateLimited().value(), 1);

  EXPECT_CALL(active_request, cancel());
  EXPECT_CALL(callbacks, onFailure_());
  EXPECT_CALL(new_active_request, cancel());
  EXPECT_CALL(new_callbacks, onFailure_());
  EXPECT_CALL(*new_client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, BasicWithReadPolicy) {
  testReadPolicy(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                     ConnPoolSettings::PREFER_MASTER,
                 NetworkFilters::Common::Redis::Client::ReadPolicy::PreferPrimary);
  testReadPolicy(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::REPLICA,
      NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  testReadPolicy(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                     ConnPoolSettings::PREFER_REPLICA,
                 NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);
  testReadPolicy(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::ANY,
      NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
};

TEST_F(RedisConnPoolImplTest, Hashtagging) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;

  auto expectHashKey = [](const std::string& s) {
    return [s](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
      EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2(s));
      return nullptr;
    };
  };

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Invoke(expectHashKey("foo")));
  conn_pool_->makeRequest("{foo}.bar", value, callbacks, transaction_);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{}{bar}")));
  conn_pool_->makeRequest("foo{}{bar}", value, callbacks, transaction_);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Invoke(expectHashKey("{bar")));
  conn_pool_->makeRequest("foo{{bar}}zap", value, callbacks, transaction_);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Invoke(expectHashKey("bar")));
  conn_pool_->makeRequest("foo{bar}{zap}", value, callbacks, transaction_);

  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, HashtaggingNotEnabled) {
  InSequence s;

  setup(true, false); // Test with hashtagging not enabled.

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;

  auto expectHashKey = [](const std::string& s) {
    return [s](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
      EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2(s));
      return nullptr;
    };
  };

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("{foo}.bar")));
  conn_pool_->makeRequest("{foo}.bar", value, callbacks, transaction_);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{}{bar}")));
  conn_pool_->makeRequest("foo{}{bar}", value, callbacks, transaction_);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{{bar}}zap")));
  conn_pool_->makeRequest("foo{{bar}}zap", value, callbacks, transaction_);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{bar}{zap}")));
  conn_pool_->makeRequest("foo{bar}{zap}", value, callbacks, transaction_);

  tls_.shutdownThread();
};

// ConnPool created when no cluster exists at creation time. Dynamic cluster creation and removal
// work correctly.
TEST_F(RedisConnPoolImplTest, NoClusterAtConstruction) {
  InSequence s;

  setup(false);

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_EQ(nullptr, request);

  // Now add the cluster. Request to the cluster should succeed.
  {
    Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
      return cm_.thread_local_cluster_;
    };
    update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_.info()->name(), command);
  }
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(true, "foo", 9631199822919835226U);

  // Remove the cluster. Request to the cluster should fail.
  EXPECT_CALL(*client_, close());
  update_callbacks_->onClusterRemoval("fake_cluster");
  request = conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_EQ(nullptr, request);

  // Add a cluster we don't care about.
  NiceMock<Upstream::MockThreadLocalCluster> cluster2;
  cluster2.cluster_.info_->name_ = "cluster2";
  {
    Upstream::ThreadLocalClusterCommand command = [&cluster2]() -> Upstream::ThreadLocalCluster& {
      return cluster2;
    };
    update_callbacks_->onClusterAddOrUpdate(cluster2.cluster_.info()->name(), command);
  }

  // Add the cluster back. Request to the cluster should succeed.
  {
    Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
      return cm_.thread_local_cluster_;
    };
    update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_.info()->name(), command);
  }
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(true, "foo", 9631199822919835226U);

  // Remove a cluster we don't care about. Request to the cluster should succeed.
  update_callbacks_->onClusterRemoval("some_other_cluster");
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(false, "foo", 9631199822919835226U);

  // Update the cluster. This should count as a remove followed by an add. Request to the cluster
  // should succeed.
  EXPECT_CALL(*client_, close());
  {
    Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
      return cm_.thread_local_cluster_;
    };
    update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_.info()->name(), command);
  }
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(true, "foo", 9631199822919835226U);

  // Remove the cluster to make sure we safely destruct with no cluster.
  EXPECT_CALL(*client_, close());
  update_callbacks_->onClusterRemoval("fake_cluster");
}

// ConnPool created when no cluster exists at creation time. Dynamic cluster
// creation and removal work correctly. Username and password are updated with
// dynamic cluster.
TEST_F(RedisConnPoolImplTest, AuthInfoUpdate) {
  InSequence s;

  // Initialize username and password.
  auth_username_ = "testusername";
  auth_password_ = "testpassword";

  setup(false);

  EXPECT_EQ(auth_username_, getAuthUsername());
  EXPECT_EQ(auth_password_, getAuthPassword());

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_EQ(nullptr, request);

  // The username and password will be updated to empty when cluster updates
  auth_username_ = "";
  auth_password_ = "";

  // Now add the cluster. Request to the cluster should succeed.
  {
    Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
      return cm_.thread_local_cluster_;
    };
    update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_.info()->name(), command);
  }
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(true, "foo", 9631199822919835226U);

  EXPECT_EQ(auth_username_, getAuthUsername());
  EXPECT_EQ(auth_password_, getAuthPassword());

  // Remove the cluster to make sure we safely destruct with no cluster.
  EXPECT_CALL(*client_, close());
  update_callbacks_->onClusterRemoval("fake_cluster");
}

// This test removes a single host from the ConnPool after learning about 2 hosts from the
// associated load balancer.
TEST_F(RedisConnPoolImplTest, HostRemove) {
  setup();

  MockPoolCallbacks callbacks;
  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  std::shared_ptr<Upstream::MockHost> host1(new Upstream::MockHost());
  std::shared_ptr<Upstream::MockHost> host2(new Upstream::MockHost());
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(ByMove(Upstream::HostSelectionResponse{host1})));
  EXPECT_CALL(*this, create_(Eq(host1))).WillOnce(Return(client1));

  Common::Redis::Client::MockPoolRequest active_request1;
  EXPECT_CALL(*host1, address()).WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client1, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_NE(nullptr, request1);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(ByMove(Upstream::HostSelectionResponse{host2})));
  EXPECT_CALL(*this, create_(Eq(host2))).WillOnce(Return(client2));

  Common::Redis::Client::MockPoolRequest active_request2;
  EXPECT_CALL(*host2, address()).WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client2, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequest("bar", value, callbacks, transaction_);
  EXPECT_NE(nullptr, request2);

  EXPECT_CALL(*client2, close());
  EXPECT_CALL(*host2, address()).WillRepeatedly(Return(test_address_));
  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, {host2});

  EXPECT_CALL(active_request1, cancel());
  EXPECT_CALL(active_request2, cancel());
  EXPECT_CALL(*client1, close());
  EXPECT_CALL(callbacks, onFailure_()).Times(2);
  tls_.shutdownThread();

  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(host1.get()));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(host2.get()));
  testing::Mock::AllowLeak(host1.get());
  testing::Mock::AllowLeak(host2.get());
}

// This test removes a host from a ConnPool that was never added in the first place. No errors
// should be encountered.
TEST_F(RedisConnPoolImplTest, HostRemovedNeverAdded) {
  InSequence s;

  setup();

  std::shared_ptr<Upstream::MockHost> host1(new Upstream::MockHost());
  auto host1_test_address = *Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
  EXPECT_CALL(*host1, address()).WillOnce(Return(host1_test_address));
  EXPECT_NO_THROW(cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks(
      {}, {host1}));
  EXPECT_EQ(hostAddressMap().size(), 0);

  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, DeleteFollowedByClusterUpdateCallback) {
  setup();
  conn_pool_.reset();

  std::shared_ptr<Upstream::Host> host(new Upstream::MockHost());
  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, {host});
}

TEST_F(RedisConnPoolImplTest, NoHost) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(ByMove(Upstream::HostSelectionResponse{nullptr})));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_EQ(nullptr, request);

  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, RemoteClose) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
  conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);

  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  client->runHighWatermarkCallbacks();
  client->runLowWatermarkCallbacks();
  client->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(active_request, cancel());
  EXPECT_CALL(callbacks, onFailure_());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MakeRequestToHost) {
  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest active_request1;
  Common::Redis::Client::MockPoolRequest active_request2;
  Common::Redis::Client::MockClientCallbacks callbacks1;
  Common::Redis::Client::MockClientCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  {
    InSequence s;

    setup(false);

    // There is no cluster yet, so makeRequestToHost() should fail.
    EXPECT_EQ(nullptr, conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1));
    // Add the cluster now.
    {
      Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
        return cm_.thread_local_cluster_;
      };
      update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_.info()->name(), command);
    }

    EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
    EXPECT_CALL(*client1, makeRequest_(Ref(value), Ref(callbacks1)))
        .WillOnce(Return(&active_request1));
    Common::Redis::Client::PoolRequest* request1 =
        conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
    EXPECT_EQ(&active_request1, request1);
    EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

    // IPv6 address returned from Redis server will not have square brackets
    // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
    // the address.
    EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
    EXPECT_CALL(*client2, makeRequest_(Ref(value), Ref(callbacks2)))
        .WillOnce(Return(&active_request2));
    Common::Redis::Client::PoolRequest* request2 =
        conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
    EXPECT_EQ(&active_request2, request2);
    EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

    // Test with a badly specified host address (no colon, no address, no port).
    EXPECT_EQ(conn_pool_->makeRequestToHost("bad", value, callbacks1), nullptr);
    // Test with a badly specified IPv4 address.
    EXPECT_EQ(conn_pool_->makeRequestToHost("10.0.bad:3000", value, callbacks1), nullptr);
    // Test with a badly specified TCP port.
    EXPECT_EQ(conn_pool_->makeRequestToHost("10.0.0.1:bad", value, callbacks1), nullptr);
    // Test with a TCP port outside of the acceptable range for a 32-bit integer.
    EXPECT_EQ(conn_pool_->makeRequestToHost("10.0.0.1:4294967297", value, callbacks1),
              nullptr); // 2^32 + 1
    // Test with a TCP port outside of the acceptable range for a TCP port (0 .. 65535).
    EXPECT_EQ(conn_pool_->makeRequestToHost("10.0.0.1:65536", value, callbacks1), nullptr);
    // Test with a badly specified IPv6-like address.
    EXPECT_EQ(conn_pool_->makeRequestToHost("bad:ipv6:3000", value, callbacks1), nullptr);
    // Test with a valid IPv6 address and a badly specified TCP port (out of range).
    EXPECT_EQ(conn_pool_->makeRequestToHost("2001:470:813b:::70000", value, callbacks1), nullptr);
  }

  // We cannot guarantee which order close will be called, perform these checks unsequenced
  EXPECT_CALL(*client1, close());
  EXPECT_CALL(*client2, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MakeRequestToHostWithZeroMaxUnknownUpstreamConnectionLimit) {
  InSequence s;

  // Create a ConnPool with a max_upstream_unknown_connections setting of 0.
  setup(true, true, 0);

  Common::Redis::RespValue value;
  Common::Redis::Client::MockClientCallbacks callbacks1;

  // The max_unknown_upstream_connections is set to 0. Request should fail.
  EXPECT_EQ(nullptr, conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1));
  EXPECT_EQ(maxUpstreamUnknownConnectionsReached().value(), 1);
  tls_.shutdownThread();
}

// This test forces the creation of 2 hosts (one with an IPv4 address, and the other with an IPv6
// address) and pending requests using makeRequestToHost(). After their creation, "new" hosts are
// discovered, and the original hosts are put aside to drain. The test then verifies the drain
// logic.
TEST_F(RedisConnPoolImplTest, HostsAddedAndRemovedWithDraining) {
  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest auth_request1, active_request1;
  Common::Redis::Client::MockPoolRequest auth_request2, active_request2;
  Common::Redis::Client::MockClientCallbacks callbacks1;
  Common::Redis::Client::MockClientCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
  EXPECT_CALL(*client1, makeRequest_(Ref(value), Ref(callbacks1)))
      .WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
  EXPECT_EQ(&active_request1, request1);
  EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

  // IPv6 address returned from Redis server will not have square brackets
  // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
  // the address.
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(value), Ref(callbacks2)))
      .WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
  EXPECT_EQ(&active_request2, request2);
  EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

  absl::node_hash_map<std::string, Upstream::HostConstSharedPtr>& host_address_map =
      hostAddressMap();
  EXPECT_EQ(host_address_map.size(), 2); // host1 and host2 have been created.
  EXPECT_EQ(host_address_map[host1->address()->asString()], host1);
  EXPECT_EQ(host_address_map[host2->address()->asString()], host2);
  EXPECT_EQ(clientMap().size(), 2);
  EXPECT_NE(clientMap().find(host1), clientMap().end());
  EXPECT_NE(clientMap().find(host2), clientMap().end());
  void* host1_active_client = clientMap(host1);
  EXPECT_EQ(createdViaRedirectHosts().size(), 2);
  EXPECT_EQ(clientsToDrain().size(), 0);
  EXPECT_EQ(drainTimer()->enabled(), false);

  std::shared_ptr<Upstream::MockHost> new_host1(new Upstream::MockHost());
  std::shared_ptr<Upstream::MockHost> new_host2(new Upstream::MockHost());
  auto new_host1_test_address = *Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
  auto new_host2_test_address = *Network::Utility::resolveUrl("tcp://[2001:470:813b::1]:3333");
  EXPECT_CALL(*new_host1, address()).WillRepeatedly(Return(new_host1_test_address));
  EXPECT_CALL(*new_host2, address()).WillRepeatedly(Return(new_host2_test_address));
  EXPECT_CALL(*client1, active()).WillOnce(Return(true));
  EXPECT_CALL(*client2, active()).WillOnce(Return(false));
  EXPECT_CALL(*client2, close());

  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks(
      {new_host1, new_host2}, {});

  host_address_map = hostAddressMap();
  EXPECT_EQ(host_address_map.size(), 2); // new_host1 and new_host2 have been added.
  EXPECT_EQ(host_address_map[new_host1_test_address->asString()], new_host1);
  EXPECT_EQ(host_address_map[new_host2_test_address->asString()], new_host2);
  EXPECT_EQ(clientMap().size(), 0);
  EXPECT_EQ(createdViaRedirectHosts().size(), 0);
  EXPECT_EQ(clientsToDrain().size(), 1); // client2 has already been drained.
  EXPECT_EQ(clientsToDrain().front().get(), host1_active_client); // client1 is still active.
  EXPECT_EQ(drainTimer()->enabled(), true);

  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks(
      {}, {new_host1, new_host2});

  EXPECT_EQ(host_address_map.size(), 0); // new_host1 and new_host2 have been removed.
  EXPECT_EQ(clientMap().size(), 0);
  EXPECT_EQ(createdViaRedirectHosts().size(), 0);
  EXPECT_EQ(clientsToDrain().size(), 1);
  EXPECT_EQ(clientsToDrain().front().get(), host1_active_client);
  EXPECT_EQ(drainTimer()->enabled(), true);

  EXPECT_CALL(*client1, active()).WillOnce(Return(true));
  drainTimer()->disableTimer();
  drainClients();
  EXPECT_EQ(clientsToDrain().size(), 1); // Nothing happened. client1 is still active.
  EXPECT_EQ(drainTimer()->enabled(), true);

  EXPECT_CALL(*client1, active()).Times(2).WillRepeatedly(Return(false));
  EXPECT_CALL(*client1, close());
  drainTimer()->disableTimer();
  drainClients();
  EXPECT_EQ(clientsToDrain().size(), 0); // client1 has been drained and closed.
  EXPECT_EQ(drainTimer()->enabled(), false);
  EXPECT_EQ(upstreamCxDrained().value(), 1);

  tls_.shutdownThread();
}

// This test creates 2 hosts (one with an IPv4 address, and the other with an IPv6
// address) and pending requests using makeRequestToHost(). After their creation, "new" hosts are
// discovered (added), and the original hosts are put aside to drain. Destructors are then
// called on these not yet drained clients, and the underlying connections should be closed.
TEST_F(RedisConnPoolImplTest, HostsAddedAndEndWithNoDraining) {
  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest auth_request1, active_request1;
  Common::Redis::Client::MockPoolRequest auth_request2, active_request2;
  Common::Redis::Client::MockClientCallbacks callbacks1;
  Common::Redis::Client::MockClientCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
  EXPECT_CALL(*client1, makeRequest_(Ref(value), Ref(callbacks1)))
      .WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
  EXPECT_EQ(&active_request1, request1);
  EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

  // IPv6 address returned from Redis server will not have square brackets
  // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
  // the address.
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(value), Ref(callbacks2)))
      .WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
  EXPECT_EQ(&active_request2, request2);
  EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

  absl::node_hash_map<std::string, Upstream::HostConstSharedPtr>& host_address_map =
      hostAddressMap();
  EXPECT_EQ(host_address_map.size(), 2); // host1 and host2 have been created.
  EXPECT_EQ(host_address_map[host1->address()->asString()], host1);
  EXPECT_EQ(host_address_map[host2->address()->asString()], host2);
  EXPECT_EQ(clientMap().size(), 2);
  EXPECT_NE(clientMap().find(host1), clientMap().end());
  EXPECT_NE(clientMap().find(host2), clientMap().end());
  EXPECT_EQ(createdViaRedirectHosts().size(), 2);
  EXPECT_EQ(clientsToDrain().size(), 0);
  EXPECT_EQ(drainTimer()->enabled(), false);

  std::shared_ptr<Upstream::MockHost> new_host1(new Upstream::MockHost());
  std::shared_ptr<Upstream::MockHost> new_host2(new Upstream::MockHost());
  auto new_host1_test_address = *Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
  auto new_host2_test_address = *Network::Utility::resolveUrl("tcp://[2001:470:813b::1]:3333");
  EXPECT_CALL(*new_host1, address()).WillRepeatedly(Return(new_host1_test_address));
  EXPECT_CALL(*new_host2, address()).WillRepeatedly(Return(new_host2_test_address));
  EXPECT_CALL(*client1, active()).WillOnce(Return(true));
  EXPECT_CALL(*client2, active()).WillOnce(Return(true));

  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks(
      {new_host1, new_host2}, {});

  host_address_map = hostAddressMap();
  EXPECT_EQ(host_address_map.size(), 2); // new_host1 and new_host2 have been added.
  EXPECT_EQ(host_address_map[new_host1_test_address->asString()], new_host1);
  EXPECT_EQ(host_address_map[new_host2_test_address->asString()], new_host2);
  EXPECT_EQ(clientMap().size(), 0);
  EXPECT_EQ(createdViaRedirectHosts().size(), 0);
  EXPECT_EQ(clientsToDrain().size(), 2); // host1 and host2 have been put aside to drain.
  EXPECT_EQ(drainTimer()->enabled(), true);

  EXPECT_CALL(*client1, close());
  EXPECT_CALL(*client2, close());
  EXPECT_CALL(*client1, active()).WillOnce(Return(true));
  EXPECT_CALL(*client2, active()).WillOnce(Return(true));
  EXPECT_EQ(upstreamCxDrained().value(), 0);

  tls_.shutdownThread();
}

// This test creates 2 hosts (one with an IPv4 address, and the other with an IPv6
// address) and pending requests using makeRequestToHost(). After their creation, "new" hosts are
// discovered (added), and the original hosts are put aside to drain. The cluster is removed and the
// underlying connections should be closed.
TEST_F(RedisConnPoolImplTest, HostsAddedAndEndWithClusterRemoval) {
  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest auth_request1, active_request1;
  Common::Redis::Client::MockPoolRequest auth_request2, active_request2;
  Common::Redis::Client::MockClientCallbacks callbacks1;
  Common::Redis::Client::MockClientCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
  EXPECT_CALL(*client1, makeRequest_(Ref(value), Ref(callbacks1)))
      .WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
  EXPECT_EQ(&active_request1, request1);
  EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

  // IPv6 address returned from Redis server will not have square brackets
  // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
  // the address.
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(value), Ref(callbacks2)))
      .WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
  EXPECT_EQ(&active_request2, request2);
  EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

  absl::node_hash_map<std::string, Upstream::HostConstSharedPtr>& host_address_map =
      hostAddressMap();
  EXPECT_EQ(host_address_map.size(), 2); // host1 and host2 have been created.
  EXPECT_EQ(host_address_map[host1->address()->asString()], host1);
  EXPECT_EQ(host_address_map[host2->address()->asString()], host2);
  EXPECT_EQ(clientMap().size(), 2);
  EXPECT_NE(clientMap().find(host1), clientMap().end());
  EXPECT_NE(clientMap().find(host2), clientMap().end());
  EXPECT_EQ(createdViaRedirectHosts().size(), 2);
  EXPECT_EQ(clientsToDrain().size(), 0);
  EXPECT_EQ(drainTimer()->enabled(), false);

  std::shared_ptr<Upstream::MockHost> new_host1(new Upstream::MockHost());
  std::shared_ptr<Upstream::MockHost> new_host2(new Upstream::MockHost());
  auto new_host1_test_address = *Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
  auto new_host2_test_address = *Network::Utility::resolveUrl("tcp://[2001:470:813b::1]:3333");
  EXPECT_CALL(*new_host1, address()).WillRepeatedly(Return(new_host1_test_address));
  EXPECT_CALL(*new_host2, address()).WillRepeatedly(Return(new_host2_test_address));
  EXPECT_CALL(*client1, active()).WillOnce(Return(true));
  EXPECT_CALL(*client2, active()).WillOnce(Return(true));

  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks(
      {new_host1, new_host2}, {});

  host_address_map = hostAddressMap();
  EXPECT_EQ(host_address_map.size(), 2); // new_host1 and new_host2 have been added.
  EXPECT_EQ(host_address_map[new_host1_test_address->asString()], new_host1);
  EXPECT_EQ(host_address_map[new_host2_test_address->asString()], new_host2);
  EXPECT_EQ(clientMap().size(), 0);
  EXPECT_EQ(createdViaRedirectHosts().size(), 0);
  EXPECT_EQ(clientsToDrain().size(), 2); // host1 and host2 have been put aside to drain.
  EXPECT_EQ(drainTimer()->enabled(), true);

  EXPECT_CALL(*client1, close());
  EXPECT_CALL(*client2, close());
  EXPECT_CALL(*client1, active()).WillOnce(Return(true));
  EXPECT_CALL(*client2, active()).WillOnce(Return(true));
  update_callbacks_->onClusterRemoval("fake_cluster");

  EXPECT_EQ(hostAddressMap().size(), 0);
  EXPECT_EQ(clientMap().size(), 0);
  EXPECT_EQ(clientsToDrain().size(), 0);
  EXPECT_EQ(upstreamCxDrained().value(), 0);

  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MakeRequestToRedisCluster) {

  envoy::config::cluster::v3::Cluster::CustomClusterType cluster_type;
  cluster_type.set_name("envoy.clusters.redis");

  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, clusterType())
      .WillOnce(Return(
          makeOptRef<const envoy::config::cluster::v3::Cluster::CustomClusterType>(cluster_type)));

  setup();

  makeSimpleRequest(true, "foo", 44950);

  makeSimpleRequest(false, "bar", 37829);

  EXPECT_CALL(*client_, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, MakeRequestToRedisClusterHashtag) {

  envoy::config::cluster::v3::Cluster::CustomClusterType cluster_type;
  cluster_type.set_name("envoy.clusters.redis");

  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, clusterType())
      .WillOnce(Return(
          makeOptRef<const envoy::config::cluster::v3::Cluster::CustomClusterType>(cluster_type)));

  setup();

  makeSimpleRequest(true, "{foo}bar", 44950);

  makeSimpleRequest(false, "foo{bar}", 37829);

  EXPECT_CALL(*client_, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, MovedRedirectionSuccess) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_value, callbacks, active_request);

  Common::Redis::Client::MockPoolRequest active_request2;
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;

  Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
  moved_response->type(Common::Redis::RespType::Error);
  moved_response->asString() = "MOVED 1111 10.1.2.3:4000";

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(*request_value), _)).WillOnce(Return(&active_request2));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "10.1.2.3:4000",
                                                  false);
  EXPECT_EQ(host1->address()->asString(), "10.1.2.3:4000");
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_succeeded_total")
                     .value());

  respond(callbacks, client2);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MovedRedirectionSuccessWithDNSEntryCached) {
  InSequence s;

  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_value, callbacks, active_request);

  Common::Redis::Client::MockPoolRequest active_request2;
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;

  Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
  moved_response->type(Common::Redis::RespType::Error);
  moved_response->asString() = "MOVED 1111 foo:6379";

  // DNS entry is cached.
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 6379);
  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks&) {
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::InCache,
            nullptr, host_info};
      }));
  EXPECT_CALL(*host_info, address()).Times(2);

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(*request_value), _)).WillOnce(Return(&active_request2));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());

  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "foo:6379", false);
  EXPECT_EQ(host1->address()->asString(), "1.2.3.4:6379");
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_succeeded_total")
                     .value());

  respond(callbacks, client2);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MovedRedirectionFailedWithDNSOverflow) {
  InSequence s;

  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_value, callbacks, active_request);

  Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
  moved_response->type(Common::Redis::RespType::Error);
  moved_response->asString() = "MOVED 1111 foo:6379";

  // DNS lookup cannot be performed.
  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks&) {
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Overflow,
            nullptr, std::nullopt};
      }));

  EXPECT_CALL(callbacks, onResponse_(_));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());

  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "foo:6379", false);

  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_failed_total")
                     .value());

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MovedRedirectionSuccessWithDNSEntryViaCallback) {
  InSequence s;

  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_value, callbacks, active_request);

  Common::Redis::Client::MockPoolRequest active_request2;
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;

  Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
  moved_response->type(Common::Redis::RespType::Error);
  moved_response->asString() = "MOVED 1111 foo:6379";

  // DNS entry is not cached.
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  std::optional<std::reference_wrapper<
      Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks>>
      saved_callbacks;

  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks& callbacks) {
        saved_callbacks = callbacks;
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, std::nullopt};
      }));

  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "foo:6379", false);

  EXPECT_CALL(*handle, onDestroy());

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 6379);
  EXPECT_CALL(*host_info, address()).Times(2);

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(*request_value), _)).WillOnce(Return(&active_request2));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());

  saved_callbacks.value().get().onLoadDnsCacheComplete(host_info);

  EXPECT_EQ(host1->address()->asString(), "1.2.3.4:6379");
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_succeeded_total")
                     .value());

  respond(callbacks, client2);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

// A request mid async-DNS-redirect (Loading, at the FIFO front) must
// NOT be reclaimed when a later request completes out of order. Its request_handler_ is null
// but the request is not complete — popping it destroys it with no callback and the downstream
// hangs.
TEST_F(RedisConnPoolImplTest, DnsRedirectLoadingEntryNotPoppedByOutOfOrderCompletion) {
  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  // A (front) and B (behind it) on the same host/client.
  Common::Redis::RespValueSharedPtr request_a = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_a;
  MockPoolCallbacks callbacks_a;
  auto* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_a, callbacks_a, active_a);
  Common::Redis::RespValueSharedPtr request_b = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_b;
  MockPoolCallbacks callbacks_b;
  makeRequest(client, request_b, callbacks_b, active_b, /*create_client=*/false);
  EXPECT_EQ(2, pendingRequestsSize());

  // A gets -MOVED and enters the async DNS (Loading) window: request_handler_ cleared, entry marked
  // in_dns_redirect_. Capture the continuation.
  auto* handle = new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  std::optional<std::reference_wrapper<
      Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks>>
      saved_callbacks;
  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks& callbacks) {
        saved_callbacks = callbacks;
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, std::nullopt};
      }));
  Common::Redis::RespValuePtr moved_a{new Common::Redis::RespValue()};
  moved_a->type(Common::Redis::RespType::Error);
  moved_a->asString() = "MOVED 1111 foo:6379";
  client->client_callbacks_.front()->onRedirection(std::move(moved_a), "foo:6379", false);

  // B completes out of order. With the fix A (front, in_dns_redirect_) survives and blocks the
  // FIFO, so both entries remain. Without it, onRequestCompleted would pop A (and B), destroying A.
  EXPECT_CALL(callbacks_b, onResponse_(_));
  client->client_callbacks_.back()->onResponse(std::make_unique<Common::Redis::RespValue>());
  EXPECT_EQ(2, pendingRequestsSize()); // A survived — H1 discriminator

  // A's DNS lands -> redirect completes -> A delivers and both entries drain.
  auto* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockPoolRequest active_a2;
  Upstream::HostConstSharedPtr host1;
  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 6379);
  EXPECT_CALL(*handle, onDestroy());
  EXPECT_CALL(*host_info, address()).Times(2);
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(*request_a), _)).WillOnce(Return(&active_a2));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
  saved_callbacks.value().get().onLoadDnsCacheComplete(host_info);

  EXPECT_CALL(callbacks_a, onResponse_(_)); // A delivered — proves it survived the premature pop
  client2->client_callbacks_.back()->onResponse(std::make_unique<Common::Redis::RespValue>());
  EXPECT_EQ(0, pendingRequestsSize());

  EXPECT_CALL(*client, close());
  EXPECT_CALL(*client2, close());
  tls_.shutdownThread();
}

// Regression: a request parked mid DNS-redirect (Loading) is attached to NO client
// (request_handler_ cleared, in_dns_redirect_ set, only the DNS handle live), so closeAllClients()
// cannot reach it. onClusterRemoval must therefore drain pending_requests_ like ~ThreadLocalPool
// does — ~PendingRequest then delivers onFailure() to the still-alive downstream and cancels the
// DNS handle, instead of leaving the entry to await a DNS continuation for a cluster that is gone
// (and, at the FIFO front, block the requests behind it).
TEST_F(RedisConnPoolImplTest, DnsLoadingRequestFailedOnClusterRemoval) {
  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  Common::Redis::RespValueSharedPtr request_a = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_a;
  MockPoolCallbacks callbacks_a;
  auto* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_a, callbacks_a, active_a);

  // -MOVED sends the request into the async DNS (Loading) window: request_handler_ cleared, entry
  // marked in_dns_redirect_, only the DNS handle live.
  auto* handle = new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks&) {
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, std::nullopt};
      }));
  Common::Redis::RespValuePtr moved_a{new Common::Redis::RespValue()};
  moved_a->type(Common::Redis::RespType::Error);
  moved_a->asString() = "MOVED 1111 foo:6379";
  client->client_callbacks_.front()->onRedirection(std::move(moved_a), "foo:6379", false);
  EXPECT_EQ(1, pendingRequestsSize());

  // Cluster removed while the request is still Loading: the drain fails it (onFailure + the DNS
  // lookup cancelled via the handle's destruction) and then closeAllClients() closes the client.
  EXPECT_CALL(*handle, onDestroy());
  EXPECT_CALL(callbacks_a, onFailure_());
  EXPECT_CALL(*client, close());
  update_callbacks_->onClusterRemoval("fake_cluster");
  EXPECT_EQ(0, pendingRequestsSize());

  tls_.shutdownThread();
}

// Cancel() during the async DNS-redirect (Loading) window must
// cancel the in-flight DNS lookup, so its continuation can never fire into the (freed) pool
// callbacks (UAF). Reproduced with the redirected request NOT at the FIFO front (a pending request
// ahead of it), so cancel() cannot lean on onRequestCompleted popping+destroying it to release the
// handle.
TEST_F(RedisConnPoolImplTest, DnsRedirectCancelDuringLoadingCancelsDnsLookup) {
  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  // B (front) stays pending, blocking the FIFO so A behind it is never popped by
  // onRequestCompleted.
  Common::Redis::RespValueSharedPtr request_b = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_b;
  MockPoolCallbacks callbacks_b;
  auto* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_b, callbacks_b, active_b);

  // A behind B, capturing its PoolRequest handle so we can cancel it directly.
  Common::Redis::RespValueSharedPtr request_a = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_a;
  MockPoolCallbacks callbacks_a;
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext*) -> Upstream::HostConstSharedPtr {
        return cm_.thread_local_cluster_.lb_.host_;
      }));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Ref(*request_a), _)).WillOnce(Return(&active_a));
  auto* handle_a = conn_pool_->makeRequest("hash_key", request_a, callbacks_a, transaction_);
  ASSERT_NE(nullptr, handle_a);

  // A enters the async DNS window.
  auto* handle = new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks&) {
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, std::nullopt};
      }));
  Common::Redis::RespValuePtr moved_a{new Common::Redis::RespValue()};
  moved_a->type(Common::Redis::RespType::Error);
  moved_a->asString() = "MOVED 1111 foo:6379";
  client->client_callbacks_.back()->onRedirection(std::move(moved_a), "foo:6379", false);

  // Cancelling A during Loading must cancel the DNS lookup right here (handle onDestroy), not leave
  // it armed to fire into freed callbacks. A is behind B, so it is not popped; the fix does the
  // cancellation in cancel() itself. dns_cancelled is set from onDestroy so we never touch the
  // freed handle.
  bool dns_cancelled = false;
  EXPECT_CALL(*handle, onDestroy()).WillOnce(Invoke([&] { dns_cancelled = true; }));
  handle_a->cancel();
  EXPECT_TRUE(dns_cancelled); // H2 discriminator: DNS lookup cancelled inside cancel()

  EXPECT_CALL(active_b, cancel());
  EXPECT_CALL(callbacks_b, onFailure_());
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

// a MOVED redirect whose target needs a DNS lookup enters an async Loading window. The
// client-side PendingRequest was already popped before onRedirection fired (ClientImpl pops before
// dispatching), so the ``request_handler_`` PoolRequest* dangles; the Loading branch clears it. A
// teardown DURING that window must therefore NOT call ``request_handler_->cancel()`` on the freed
// handle — a use-after-free. Here the DNS lookup is left outstanding (never completed) and the pool
// is torn down; the stale upstream handle must not be cancelled.
TEST_F(RedisConnPoolImplTest, MovedRedirectionTeardownDuringDNSLoadingDoesNotUseFreedHandle) {
  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_value, callbacks, active_request);

  Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
  moved_response->type(Common::Redis::RespType::Error);
  moved_response->asString() = "MOVED 1111 foo:6379";

  // The redirect target is not cached -> the lookup returns Loading and stays outstanding (we never
  // complete it). The Loading branch clears request_handler_.
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks&) {
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, std::nullopt};
      }));
  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "foo:6379", false);

  // Tear the pool down while the DNS lookup is still Loading. ~PendingRequest must release the live
  // DNS handle but must NOT cancel the already-consumed upstream handle (request_handler_ was
  // cleared in the Loading branch). Without the fix this would be active_request.cancel() on a
  // freed PoolRequest. Because this destruction is NOT a caller cancel() (in_dns_redirect_ is still
  // set), the dtor delivers a terminal onFailure so a still-alive downstream is not left hanging.
  EXPECT_CALL(active_request, cancel()).Times(0);
  EXPECT_CALL(*handle, onDestroy());
  EXPECT_CALL(callbacks, onFailure_());
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MovedRedirectionFailedWithDNSEntryViaCallback) {
  InSequence s;

  auto dns_cache = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsCache>();
  setup(true, true, 100, dns_cache);

  Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_value, callbacks, active_request);

  Common::Redis::Client::MockPoolRequest active_request2;
  Upstream::HostConstSharedPtr host1;

  Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
  moved_response->type(Common::Redis::RespType::Error);
  moved_response->asString() = "MOVED 1111 foo:6379";

  // DNS entry is not cached.
  Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle* handle =
      new Extensions::Common::DynamicForwardProxy::MockLoadDnsCacheEntryHandle();
  std::optional<std::reference_wrapper<
      Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks>>
      saved_callbacks;

  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks& callbacks) {
        saved_callbacks = callbacks;
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, std::nullopt};
      }));

  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "foo:6379", false);

  EXPECT_CALL(*handle, onDestroy());
  EXPECT_CALL(callbacks, onResponse_(_));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());

  saved_callbacks.value().get().onLoadDnsCacheComplete(nullptr);

  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_failed_total")
                     .value());

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MovedRedirectionFailure) {
  InSequence s;

  setup();

  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  // Test with a badly specified host address (no colon, no address, no port).
  verifyInvalidMoveResponse(client, "bad", true);

  // Test with a badly specified IPv4 address.
  verifyInvalidMoveResponse(client, "10.0.bad:3000", false);

  // Test with a badly specified TCP port.
  verifyInvalidMoveResponse(client, "10.0.bad:3000", false);

  // Test with a TCP port outside of the acceptable range for a 32-bit integer.
  verifyInvalidMoveResponse(client, "10.0.0.1:4294967297", false); // 2^32 + 1

  // Test with a TCP port outside of the acceptable range for a TCP port (0 .. 65535).
  verifyInvalidMoveResponse(client, "10.0.0.1:65536", false);

  // Test with a badly specified IPv6-like address.
  verifyInvalidMoveResponse(client, "bad:ipv6:3000", false);

  // Test with a valid IPv6 address and a badly specified TCP port (out of range).
  verifyInvalidMoveResponse(client, "2001:470:813b:::70000", false);

  // Test an upstream error preventing the request from being sent.
  MockPoolCallbacks callbacks;
  Common::Redis::RespValueSharedPtr request3 = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request3;
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  makeRequest(client, request3, callbacks, active_request3, false);
  Common::Redis::RespValuePtr moved_response3{new Common::Redis::RespValue()};
  moved_response3->type(Common::Redis::RespType::Error);
  moved_response3->asString() = "MOVED 1111 10.1.2.3:4000";
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(*request3), _)).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks, onResponse_(Ref(moved_response3)));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
  client->client_callbacks_.back()->onRedirection(std::move(moved_response3), "10.1.2.3:4000",
                                                  false);
  EXPECT_EQ(host1->address()->asString(), "10.1.2.3:4000");

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, AskRedirectionSuccess) {
  InSequence s;

  setup();

  Common::Redis::RespValueSharedPtr request_value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  makeRequest(client, request_value, callbacks, active_request);

  Common::Redis::Client::MockPoolRequest ask_request, active_request2;
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;

  Common::Redis::RespValuePtr ask_response{new Common::Redis::RespValue()};
  ask_response->type(Common::Redis::RespType::Error);
  ask_response->asString() = "ASK 1111 10.1.2.3:4000";
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  // Verify that the request has been properly prepended with an "asking" command.
  EXPECT_CALL(*client2, makeRequest_(Ref(Common::Redis::Utility::AskingRequest::instance()), _))
      .WillOnce(Return(&ask_request));
  EXPECT_CALL(*client2, makeRequest_(Ref(*request_value), _)).WillOnce(Return(&active_request2));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
  client->client_callbacks_.back()->onRedirection(std::move(ask_response), "10.1.2.3:4000", true);
  EXPECT_EQ(host1->address()->asString(), "10.1.2.3:4000");
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_succeeded_total")
                     .value());

  respond(callbacks, client2);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, AskRedirectionFailure) {
  InSequence s;

  setup();

  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  // Test an upstream error from trying to send an "asking" command upstream.
  Common::Redis::Client::MockPoolRequest active_request3;
  Common::Redis::RespValueSharedPtr request3 = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  makeRequest(client, request3, callbacks, active_request3);
  Common::Redis::RespValuePtr ask_response3{new Common::Redis::RespValue()};
  ask_response3->type(Common::Redis::RespType::Error);
  ask_response3->asString() = "ASK 1111 10.1.2.3:4000";
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(Common::Redis::Utility::AskingRequest::instance()), _))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks, onResponse_(Ref(ask_response3)));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
  client->client_callbacks_.back()->onRedirection(std::move(ask_response3), "10.1.2.3:4000", true);
  EXPECT_EQ(host1->address()->asString(), "10.1.2.3:4000");
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_failed_total")
                     .value());

  // Test an upstream error from trying to send the original request after the "asking" command is
  // sent successfully.
  Common::Redis::Client::MockPoolRequest active_request4, active_request5;
  Common::Redis::RespValueSharedPtr request4 = std::make_shared<Common::Redis::RespValue>();
  makeRequest(client, request4, callbacks, active_request4, false);
  Common::Redis::RespValuePtr ask_response4{new Common::Redis::RespValue()};
  ask_response4->type(Common::Redis::RespType::Error);
  ask_response4->asString() = "ASK 1111 10.1.2.3:4000";
  EXPECT_CALL(*client2, makeRequest_(Ref(Common::Redis::Utility::AskingRequest::instance()), _))
      .WillOnce(Return(&active_request5));
  EXPECT_CALL(*client2, makeRequest_(Ref(*request4), _)).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks, onResponse_(Ref(ask_response4)));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
  client->client_callbacks_.back()->onRedirection(std::move(ask_response4), "10.1.2.3:4000", true);
  EXPECT_EQ(2UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_failed_total")
                     .value());

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MakeRequestAndRedirectFollowedByDelete) {
  cm_.initializeThreadLocalClusters({"fake_cluster"});
  tls_.defer_delete_ = true;
  cluster_refresh_manager_ =
      std::make_shared<NiceMock<Extensions::Common::Redis::MockClusterRefreshManager>>();
  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(store_.symbolTable());
  conn_pool_ = std::make_shared<InstanceImpl>(
      cluster_name_, cm_, *this, tls_,
      Common::Redis::Client::createConnPoolSettings(20, true, true, 100, read_policy_), api_,
      store_.rootScope(), redis_command_stats, cluster_refresh_manager_, nullptr, std::nullopt,
      std::nullopt, /*local_zone=*/"", Common::Redis::RespProtocolVersion::Resp2);
  conn_pool_->init();

  auto& local_pool = threadLocalPool();
  conn_pool_.reset();

  // Request
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return this->cm_.thread_local_cluster_.lb_.host_;
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(this->test_address_));
  EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
  EXPECT_NE(nullptr, local_pool.makeRequest("hash_key", value, callbacks, transaction_));

  // Move redirection.
  Common::Redis::Client::MockPoolRequest active_request2;
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Common::Redis::RespValuePtr moved_response{new Common::Redis::RespValue()};
  moved_response->type(Common::Redis::RespType::Error);
  moved_response->asString() = "MOVED 1111 10.1.2.3:4000";

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client2)));
  EXPECT_CALL(*client2, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request2));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, cluster());
  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "10.1.2.3:4000",
                                                  false);
  EXPECT_EQ(host1->address()->asString(), "10.1.2.3:4000");
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.lb_.host_->cluster_.stats_store_
                     .counter("upstream_internal_redirect_succeeded_total")
                     .value());
  EXPECT_CALL(callbacks, onResponse_(_));
  client2->client_callbacks_.back()->onResponse(std::make_unique<Common::Redis::RespValue>());

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

// Plumbing pin (normal request path): the filter-level RESP version handed to InstanceImpl's
// ctor must flow through ThreadLocalPool::upstream_protocol_version_ into
// client_factory_.create() so ClientImpl can drive the RESP3 init pipeline. The test exercises
// the full propagation — ctor → ThreadLocalPool → factory.create — rather than poking the TLS
// pool's protocol field directly. Client-side HELLO 3 callback contracts (success, failure,
// redirection) live in client_impl_test.cc; the conn pool's only obligation is forwarding.
TEST_F(RedisConnPoolImplTest, ClientFactoryReceivesResp3OnNormalRequestPath) {
  InSequence s;
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_NE(nullptr, request);

  EXPECT_EQ(Common::Redis::RespProtocolVersion::Resp3, last_upstream_protocol_version_);
  EXPECT_FALSE(last_is_transaction_client_);
  // The counter handed to the client factory must be the cluster's
  // upstream_resp3_hello_failure counter (same object, not just non-null).
  EXPECT_EQ(&upstreamResp3HelloFailure(), last_upstream_resp3_hello_failure_);

  EXPECT_CALL(active_request, cancel());
  EXPECT_CALL(callbacks, onFailure_());
  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
}

// Plumbing pin (transaction client path): conn_pool_impl.cc has a second
// client_factory_.create() call inside makeRequestToHost when the request carries an active
// Transaction. A copy-paste regression that left this path on the cluster-level proto enum or
// hard-coded RESP2 would let upstream RESP3 silently break inside MULTI/EXEC; this test pins
// the propagation against the same InstanceImpl ctor argument as the normal path above.
TEST_F(RedisConnPoolImplTest, ClientFactoryReceivesResp3OnTransactionClientPath) {
  InSequence s;
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  Common::Redis::Client::MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  // Drive the transaction-client branch: active_ true, connection_established_ false, one slot.
  Common::Redis::Client::Transaction transaction(nullptr);
  transaction.start();
  transaction.clients_.resize(1);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction);
  EXPECT_NE(nullptr, request);

  EXPECT_EQ(Common::Redis::RespProtocolVersion::Resp3, last_upstream_protocol_version_);
  EXPECT_TRUE(last_is_transaction_client_);
  // The counter handed to the client factory must be the cluster's
  // upstream_resp3_hello_failure counter (same object, not just non-null).
  EXPECT_EQ(&upstreamResp3HelloFailure(), last_upstream_resp3_hello_failure_);

  EXPECT_CALL(active_request, cancel());
  EXPECT_CALL(callbacks, onFailure_());
  // The transaction owns the created client; closing the transaction releases it cleanly.
  transaction.close();
  tls_.shutdownThread();
}

// Pub/sub gating: subscriptionRegistryShared() must return nullptr when the listener is not
// RESP3 (Push frames are RESP3-only on the wire). This is the gate that disables pub/sub on
// RESP2 listeners. The RESP version is fixed at pool construction from the listener config,
// so the RESP2 and RESP3 poles are pinned by separate pools.
TEST_F(RedisConnPoolImplTest, SubscriptionRegistrySharedReturnsNullForNonResp3) {
  setup();
  // Default listener is RESP2 → no registry.
  EXPECT_EQ(nullptr, conn_pool_->subscriptionRegistryShared());
  tls_.shutdownThread();
}

// RESP3 pool → registry materializes lazily on first call and is cached.
TEST_F(RedisConnPoolImplTest, SubscriptionRegistryLazyInitAndCachedOnResp3) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);
  auto registry = conn_pool_->subscriptionRegistryShared();
  EXPECT_NE(nullptr, registry);
  // Subsequent call returns the same instance (lazy init cached).
  EXPECT_EQ(registry, conn_pool_->subscriptionRegistryShared());
  tls_.shutdownThread();
}

// ②: a data request and a subscription to the SAME host use SEPARATE upstream connections
// (client_map_ vs subscription_client_map_). Because the subscription connection never carries a
// pending data request, a non-Push subscribe error on it can never be wrongly popped by an
// unrelated GET — the cross-delivery hazard is removed structurally.
TEST_F(RedisConnPoolImplTest, SubscriptionUsesSeparateConnectionFromData) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  // Two DISTINCT clients to the same host: the first for the data path, the second for the
  // subscription path.
  auto* data_client = new NiceMock<Common::Redis::Client::MockClient>();
  auto* sub_client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(data_client)).WillOnce(Return(sub_client));

  // Two host selections: one for the data request, one for the sharded subscription (both resolve
  // to the same host; HostSelectionResponse is move-only so each WillOnce builds a fresh one).
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockPoolRequest active_request;
  EXPECT_CALL(*data_client, makeRequest_(_, _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* req =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_NE(nullptr, req);

  // Subscription routes to the SECOND client (sub_client) via sendCommand — never through
  // data_client. data_client sees no setPushCallbacks / sendCommand.
  EXPECT_CALL(*sub_client, setPushCallbacks(_));
  EXPECT_CALL(*sub_client, sendCommand(_));
  EXPECT_CALL(*data_client, setPushCallbacks(_)).Times(0);
  EXPECT_CALL(*data_client, sendCommand(_)).Times(0);
  NiceMock<Common::Redis::Client::MockPushMessageCallbacks> push_cb;
  // the resolve+send composite is gone — resolve the placement, then send to it. The resolve
  // returns the chosen shard host and the send routes to the subscription client.
  Upstream::HostConstSharedPtr chosen = threadLocalPool().chooseUpstreamHostForChannel("chan");
  EXPECT_NE(nullptr, chosen);
  EXPECT_TRUE(threadLocalPool().sendUpstreamSsubscribeToHost("chan", push_cb, chosen));

  EXPECT_CALL(active_request, cancel());
  req->cancel();
  EXPECT_CALL(*data_client, close());
  EXPECT_CALL(*sub_client, close());
  tls_.shutdownThread();
}

// SendUpstreamSunsubscribe must NOT create a subscription connection when none
// exists for the host. A freshly created connection has no push_callbacks_ installed (only
// getOrCreateSubscriptionClient does that), so its SUNSUBSCRIBE ack Push would be dropped and
// permanently poison the host's control FIFO. It must find-only and return NotSent.
TEST_F(RedisConnPoolImplTest, SunsubscribeWithoutSubscriptionConnectionReturnsNotSent) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};

  // No subscription connection exists to this host: no client is created (get-or-CREATE would have
  // made a callback-less one here) and the result is NotSent.
  EXPECT_CALL(*this, create_(_)).Times(0);
  const auto result =
      threadLocalPool().sendUpstreamSunsubscribe("chan", cm_.thread_local_cluster_.lb_.host_);
  EXPECT_EQ(RedisProxy::UpstreamSubscriptionCallbacks::SunsubscribeResult::NotSent, result);

  tls_.shutdownThread();
}

// Retiring an idle subscription connection must NOT destroy the SAME host's healthy
// data connection. maybeCleanupSubscriptionMode erases the subscription entry then close()s it,
// driving the wrapper's onEvent while it is out of subscription_client_map_; without the
// data-branch identity check that onEvent falls through and erases the host's DATA client (release:
// lost request callbacks + a dangling PoolRequest* → UAF; debug: ~ClientImpl ASSERT abort).
TEST_F(RedisConnPoolImplTest, SubscriptionRetireDoesNotDestroySameHostDataClient) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  // Construct the downstream MockConnection FIRST, before any data request creates a client: its
  // MockStreamInfo allocates the test time system, and doing it up front avoids the "two different
  // time-systems" conflict with the data path.
  NiceMock<Network::MockConnection> downstream;
  auto subscriber = std::make_shared<RedisProxy::DownstreamSubscriber>(
      downstream, testDownstreamSubscriberStats());

  auto* data_client = new NiceMock<Common::Redis::Client::MockClient>();
  auto* sub_client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(data_client)).WillOnce(Return(sub_client));
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));

  // Data request -> data_client in client_map_.
  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockPoolRequest active_request;
  EXPECT_CALL(*data_client, makeRequest_(_, _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* req =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_NE(nullptr, req);
  const auto host = cm_.thread_local_cluster_.lb_.host_;
  EXPECT_EQ(1UL, clientMap().count(host)); // data client present

  // Subscription to the SAME host -> sub_client in the separate subscription_client_map_.
  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);
  EXPECT_CALL(*sub_client, setPushCallbacks(_));
  EXPECT_CALL(*sub_client, sendCommand(_)).Times(2); // SSUBSCRIBE, then SUNSUBSCRIBE below
  registry->subscribe({"ch"}, subscriber);

  // ConnectionImpl::close() raises LocalClose synchronously; make the mock do the same so the
  // retire actually drives sub_client's onEvent.
  ON_CALL(*sub_client, close()).WillByDefault(Invoke([sub_client]() {
    sub_client->raiseEvent(Network::ConnectionEvent::LocalClose);
  }));

  // Unsubscribe the last channel: SUNSUBSCRIBE + retire the now-idle subscription connection, whose
  // close() drives onEvent. The data client must be untouched.
  registry->unsubscribe({"ch"}, subscriber);
  EXPECT_EQ(1UL, clientMap().count(host)); // same-host data client NOT destroyed

  // The data client still serves cleanly (no dangling request).
  EXPECT_CALL(active_request, cancel());
  req->cancel();
  EXPECT_CALL(*data_client, close());
  tls_.shutdownThread();
}

// an INVOLUNTARY subscription-connection close must mark the host's channels for re-subscribe
// SYNCHRONOUSLY while KEEPING their channel->host owner, posting only the backoff arm. The
// pre-fix code posted the owner-map wipe (forgetHostChannels) too, so a SUBSCRIBE hashing to the
// same host in the SAME event-loop iteration recorded a fresh owner that the deferred wipe then
// erased — stranding the channel behind a stale-generation ack until a 10s timeout. The owner must
// survive the close.
TEST_F(RedisConnPoolImplTest, InvoluntarySubscriptionCloseKeepsChannelOwnerSynchronously) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  NiceMock<Network::MockConnection> downstream;
  auto subscriber = std::make_shared<RedisProxy::DownstreamSubscriber>(
      downstream, testDownstreamSubscriberStats());

  auto* sub_client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(sub_client));
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));

  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);
  EXPECT_CALL(*sub_client, setPushCallbacks(_));
  EXPECT_CALL(*sub_client, sendCommand(_)); // SSUBSCRIBE
  registry->subscribe({"ch"}, subscriber);
  EXPECT_EQ(1UL, registry->channelHosts().count("ch")); // owner recorded synchronously on send

  // Involuntary loss of the subscription connection (NOT a client unsubscribe / planned removal):
  // onEvent marks "ch" for re-subscribe synchronously and posts only the backoff arm. The owner
  // must NOT be wiped (the pre-fix deferred forgetHostChannels would have erased it).
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(tls_.dispatcher_, post(_)).WillRepeatedly([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  sub_client->raiseEvent(Network::ConnectionEvent::RemoteClose);

  // Owner survives the SYNCHRONOUS half of onEvent (markHostChannelsForResubscribe keeps it).
  EXPECT_EQ(1UL, registry->channelHosts().count("ch"));
  // ...and MUST survive the DEFERRED half too. The pre-fix code posted the forgetHostChannels owner
  // WIPE into this deferred lambda; the fix leaves only the backoff arm here. Running every posted
  // callback is what gives the test its discriminating power: pre-fix the wipe would clear the
  // owner (count -> 0, fail), post-fix only the harmless backoff arm runs (count stays 1).
  for (auto& cb : posted) {
    cb();
  }
  EXPECT_EQ(1UL, registry->channelHosts().count("ch"));

  tls_.shutdownThread();
}

// R8-12 regression, established-transaction branch: once connection_established_ is true the pool
// REUSES transaction.clients_[idx]; if that leg was never created (mirror runtime_fraction did not
// sample, or the establishing MULTI failed) the slot is null and the request must fail cleanly
// instead of dereferencing it.
TEST_F(RedisConnPoolImplTest, TransactionEstablishedNullClientLegFailsRequest) {
  setup();

  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  Common::Redis::Client::Transaction transaction(nullptr);
  transaction.start();
  transaction.connection_established_ = true;
  transaction.clients_.resize(1); // clients_[0] stays null

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*this, create_(_)).Times(0);
  EXPECT_EQ(nullptr, conn_pool_->makeRequest("hash_key", value, callbacks, transaction));

  transaction.close();
  tls_.shutdownThread();
}

// Guard branches of the deferred-close primitive: a null host and a host with no live
// subscription connection are both no-ops — nothing is queued and no timer is armed.
TEST_F(RedisConnPoolImplTest, CloseSubscriptionConnectionGuardsNoOp) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);
  auto& pool = threadLocalPool();

  pool.closeSubscriptionConnection(nullptr);
  EXPECT_TRUE(pool.hosts_pending_sub_close_.empty());

  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  pool.closeSubscriptionConnection(cm_.thread_local_cluster_.lb_.host_);
  EXPECT_TRUE(pool.hosts_pending_sub_close_.empty());

  // Idle-retire guards share the same no-op shape: null host / no subscription connection.
  EXPECT_FALSE(pool.retireSubscriptionConnectionIfIdle(nullptr));
  EXPECT_FALSE(pool.retireSubscriptionConnectionIfIdle(cm_.thread_local_cluster_.lb_.host_));

  // requestTopologyRefresh is a fire-and-forget nudge; with or without a refresh manager it must
  // not crash or mutate pool state.
  pool.requestTopologyRefresh();

  // Null-host guards on the send primitives.
  Common::Redis::Client::MockPushMessageCallbacks push_cb;
  EXPECT_FALSE(pool.sendUpstreamSsubscribeToHost("ch", push_cb, nullptr));
  EXPECT_EQ(RedisProxy::UpstreamSubscriptionCallbacks::SunsubscribeResult::NotSent,
            pool.sendUpstreamSunsubscribe("ch", nullptr));

  tls_.shutdownThread();
}

// A queued deferred close whose connection is gone by flush time (closed for another reason before
// the 0-delay timer fired) is skipped: the map entry is discarded and no close is issued.
TEST_F(RedisConnPoolImplTest, FlushDeferredCloseSkipsAlreadyGoneConnection) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  NiceMock<Network::MockConnection> downstream;
  auto subscriber = std::make_shared<RedisProxy::DownstreamSubscriber>(
      downstream, testDownstreamSubscriberStats());
  auto* sub_client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(sub_client));
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);
  EXPECT_CALL(*sub_client, setPushCallbacks(_));
  EXPECT_CALL(*sub_client, sendCommand(_));
  registry->subscribe({"ch"}, subscriber);

  auto& pool = threadLocalPool();
  pool.closeSubscriptionConnection(cm_.thread_local_cluster_.lb_.host_);
  EXPECT_EQ(1UL, pool.hosts_pending_sub_close_.size());

  // The connection dies on its own before the deferred flush runs (genuine remote loss); onEvent
  // erases the map entry.
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(tls_.dispatcher_, post(_)).WillRepeatedly([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  sub_client->raiseEvent(Network::ConnectionEvent::RemoteClose);

  // Flush finds the host absent from subscription_client_map_ and skips — no crash, queue drained.
  pool.flushDeferredSubscriptionCloses();
  EXPECT_TRUE(pool.hosts_pending_sub_close_.empty());

  for (auto& cb : posted) {
    cb();
  }
  tls_.shutdownThread();
}

// Cluster removal with the pub/sub registry attached: the registry is cleared and released as part
// of the teardown, and a host-set membership update walks the registry's topology handler through
// the posted callback.
TEST_F(RedisConnPoolImplTest, ClusterRemovalClearsSubscriptionRegistry) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);

  // Membership update -> the pool posts the registry topology walk; run the posted callback.
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(tls_.dispatcher_, post(_)).WillRepeatedly([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, {});
  for (auto& cb : posted) {
    cb();
  }
  posted.clear();

  update_callbacks_->onClusterRemoval("fake_cluster");
  tls_.shutdownThread();
}

// closeSubscriptionConnection (used when a re-routed channel's old owner refuses its SUNSUBSCRIBE)
// must (a) queue the close DEFERRED — its caller runs on the client's own reply stack, so an inline
// close would self-destruct that client mid-callback — and (b) close as a GENUINE loss (no
// planned_removal_), so onEvent re-subscribes the host's remaining channels instead of zombie-ing
// them.
TEST_F(RedisConnPoolImplTest, CloseSubscriptionConnectionDefersAndReSubscribes) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  NiceMock<Network::MockConnection> downstream;
  auto subscriber = std::make_shared<RedisProxy::DownstreamSubscriber>(
      downstream, testDownstreamSubscriberStats());
  auto* sub_client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(sub_client));
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);
  EXPECT_CALL(*sub_client, setPushCallbacks(_));
  EXPECT_CALL(*sub_client, sendCommand(_)); // SSUBSCRIBE
  registry->subscribe({"ch"}, subscriber);
  ASSERT_EQ(1UL, registry->channelHosts().count("ch"));

  auto& pool = threadLocalPool();

  // (a) Deferred: closeSubscriptionConnection QUEUES the host and does NOT close the client inline.
  // Its real caller runs on this client's own reply stack, so an inline close would be a
  // self-destruct reentrancy.
  EXPECT_CALL(*sub_client, close()).Times(0);
  pool.closeSubscriptionConnection(cm_.thread_local_cluster_.lb_.host_);
  EXPECT_EQ(1UL, pool.hosts_pending_sub_close_.size());
  testing::Mock::VerifyAndClearExpectations(sub_client);

  // Firing the deferred close (the pool timer callback) closes the source host's subscription
  // client. close() drives onEvent synchronously as a GENUINE loss (planned_removal_ stays unset),
  // so the channel owner is KEPT (markHostChannelsForResubscribe) for re-subscribe and only the
  // backoff arm is posted. A planned removal would instead skip re-subscribe and zombie it.
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(tls_.dispatcher_, post(_)).WillRepeatedly([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  EXPECT_CALL(*sub_client, close()); // close() -> raiseEvent(LocalClose) -> onEvent (genuine loss)
  pool.flushDeferredSubscriptionCloses();
  EXPECT_TRUE(pool.hosts_pending_sub_close_.empty());

  // (b)+(c) Owner survives the close, so the host re-subscribes instead of zombie-ing the channel.
  // A planned removal would have wiped it. It must survive the synchronous and deferred halves.
  EXPECT_EQ(1UL, registry->channelHosts().count("ch"));
  for (auto& cb : posted) {
    cb();
  }
  EXPECT_EQ(1UL, registry->channelHosts().count("ch"));

  tls_.shutdownThread();
}

// Companion to the test above: closeSubscriptionConnection pins the SPECIFIC offending client, not
// merely its host. If that host's subscription connection is replaced by a fresh, healthy one
// before the deferred timer fires, flush must leave the replacement ALONE: the offender is already
// gone (its own close reclaimed the leak) and closing the new connection would be pointless churn.
TEST_F(RedisConnPoolImplTest, DeferredSubscriptionCloseSkipsReplacedClient) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  NiceMock<Network::MockConnection> downstream;
  auto subscriber = std::make_shared<RedisProxy::DownstreamSubscriber>(
      downstream, testDownstreamSubscriberStats());
  auto* old_client = new NiceMock<Common::Redis::Client::MockClient>();
  auto* new_client = new NiceMock<Common::Redis::Client::MockClient>();
  // Two subscriptions to the same host span two connections: the first creates ``old_client``, and
  // after it is lost the second creates ``new_client`` under the same host.
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(old_client)).WillOnce(Return(new_client));
  // HostSelectionResponse is move-only, so a repeated Return() cannot copy it; hand back a fresh
  // one per call (both subscribes resolve to the same host).
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillRepeatedly([this](Upstream::LoadBalancerContext*) -> Upstream::HostSelectionResponse {
        return Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_};
      });
  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);
  EXPECT_CALL(*old_client, setPushCallbacks(_));
  EXPECT_CALL(*old_client, sendCommand(_)); // SSUBSCRIBE ch
  registry->subscribe({"ch"}, subscriber);
  ASSERT_EQ(1UL, registry->channelHosts().count("ch"));

  auto& pool = threadLocalPool();
  const Upstream::HostConstSharedPtr host = cm_.thread_local_cluster_.lb_.host_;
  const uint64_t old_connection_id = pool.subscription_client_map_.at(host)->connection_id_;

  // Schedule the deferred close; identity is pinned to the OLD client's connection_id_ now.
  pool.closeSubscriptionConnection(host);
  ASSERT_EQ(1UL, pool.hosts_pending_sub_close_.size());
  EXPECT_EQ(old_connection_id, pool.hosts_pending_sub_close_.at(host));

  // Before the timer fires, the OLD connection is lost and a fresh one replaces it via the real
  // paths: raising the old client's close removes it from the map (onEvent, genuine loss), then a
  // second SUBSCRIBE to the same host spins up ``new_client`` in that slot. Posts (the backoff arm)
  // are captured but never run; they are irrelevant to the identity check under test.
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(tls_.dispatcher_, post(_)).WillRepeatedly([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_)).Times(testing::AnyNumber());
  old_client->raiseEvent(Network::ConnectionEvent::LocalClose);
  registry->subscribe({"ch2"}, subscriber);
  // Monotonic ids guarantee the replacement differs — no reliance on allocator address behavior.
  ASSERT_NE(old_connection_id, pool.subscription_client_map_.at(host)->connection_id_);

  // Flush: the pinned identity no longer matches the current client, so the replacement is NOT
  // closed and the queue still drains.
  EXPECT_CALL(*new_client, close()).Times(0);
  pool.flushDeferredSubscriptionCloses();
  EXPECT_TRUE(pool.hosts_pending_sub_close_.empty());
  testing::Mock::VerifyAndClearExpectations(new_client);

  tls_.shutdownThread();
}

// When a subscription client can't be created because the host's connection rate
// limit is exhausted, getOrCreateClientInMap inserts a null placeholder into
// subscription_client_map_ for the lookup and then erases it itself before returning nullptr —
// otherwise teardown / onClusterRemoval later walks subscription_client_map_ and dereferences the
// null unique_ptr while closing subscription clients, crashing. Here a data request consumes the
// host's single connection token (the per-host rate limiter is shared with the subscription path),
// so the subsequent subscription send to the same host is denied.
TEST_F(RedisConnPoolImplTest, RateLimitedSubscriptionErasesNullPlaceholder) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/1,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  // The data request consumes the host's single connection token.
  auto* data_client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(data_client));
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  Common::Redis::RespValueSharedPtr value = std::make_shared<Common::Redis::RespValue>();
  MockPoolCallbacks callbacks;
  Common::Redis::Client::MockPoolRequest active_request;
  EXPECT_CALL(*data_client, makeRequest_(_, _)).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* req =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  ASSERT_NE(nullptr, req);

  // The subscription send to the SAME host is now rate-limited: no subscription client is created,
  // the send reports failure, and the null placeholder must not be left behind.
  NiceMock<Common::Redis::Client::MockPushMessageCallbacks> push_cb;
  // the resolve succeeds (a host owns the slot), but the rate-limited send to it creates no
  // subscription client and reports failure — and must not leave a null placeholder behind.
  Upstream::HostConstSharedPtr chosen = threadLocalPool().chooseUpstreamHostForChannel("chan");
  ASSERT_NE(nullptr, chosen);
  EXPECT_FALSE(threadLocalPool().sendUpstreamSsubscribeToHost("chan", push_cb, chosen));

  // Teardown must not dereference a null subscription-client placeholder.
  EXPECT_CALL(active_request, cancel());
  req->cancel();
  EXPECT_CALL(*data_client, close());
  tls_.shutdownThread();
}

// a PLANNED host removal must drive the resubscribe through exactly ONE path. The
// member-update callback posts onClusterTopologyChange (which re-routes sharded channels), and the
// removed host's subscription-connection close drives onEvent — which on a planned removal MUST
// consume the mark and skip its own resubscribe post. So a single post is queued (the topology
// change), not two. Before the fix, onEvent also posted, driving a redundant second resubscribe
// round.
TEST_F(RedisConnPoolImplTest, PlannedHostRemovalDoesNotDoubleDriveResubscribe) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  auto* client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));

  // Establish a sharded subscription THROUGH the registry so BOTH resubscribe drivers are live on a
  // host removal — an empty registry fires neither (onClusterTopologyChange needs a non-null
  // registry; onEvent's post needs a non-empty one). ssubscribe → the pool's
  // sendUpstreamSsubscribeToHost creates the dedicated subscription client on the chosen host and
  // records the channel→host map.
  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  EXPECT_CALL(*client, setPushCallbacks(_));
  EXPECT_CALL(*client, sendCommand(_));
  NiceMock<Network::MockConnection> downstream;
  auto subscriber = std::make_shared<RedisProxy::DownstreamSubscriber>(
      downstream, testDownstreamSubscriberStats());
  registry->subscribe({"ch"}, subscriber);

  // Capture posted callbacks so the resubscribe drivers can be counted. The subscription setup
  // above is synchronous (no post), so anything captured here is from the removal.
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(tls_.dispatcher_, post(_)).WillRepeatedly([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  EXPECT_CALL(*client, close());

  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks(
      {}, {cm_.thread_local_cluster_.lb_.host_});

  // Exactly one resubscribe driver (onClusterTopologyChange), not two — onEvent saw the wrapper's
  // planned-removal flag and suppressed its redundant resubscribe post (before the fix there
  // were two). The flag lives on the wrapper, which is destroyed with this close, so there is
  // nothing left to leak.
  EXPECT_EQ(1UL, posted.size());

  tls_.shutdownThread();
}

// Slot-only rebalance: a subscribed channel's hash slot migrates between two EXISTING nodes with NO
// host add/remove (Redis slot migration / CLUSTER SETSLOT). RedisCluster fires the member-update
// callback with empty deltas whenever the slot map changed, so the pool must STILL post
// onClusterTopologyChange — otherwise the migrated subscription is stranded on the old owner and
// stops receiving messages. (Regression: the post was previously gated on a non-empty host delta.)
TEST_F(RedisConnPoolImplTest, SlotOnlyRebalancePostsResubscribe) {
  setup(/*cluster_exists=*/true, /*hashtagging=*/true, /*max_unknown_conns=*/100,
        /*dns_cache=*/nullptr, /*redis_cx_rate_limit_per_sec=*/100,
        Common::Redis::RespProtocolVersion::Resp3);

  auto& host_set = *cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0);
  host_set.healthy_hosts_ = {cm_.thread_local_cluster_.lb_.host_};
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));

  auto* client = new NiceMock<Common::Redis::Client::MockClient>();
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));

  // Establish a sharded subscription so the registry is non-empty (doResubscribe would no-op an
  // empty one) and there is a channel whose owner could move.
  auto registry = conn_pool_->subscriptionRegistryShared();
  ASSERT_NE(nullptr, registry);
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(Upstream::HostSelectionResponse{cm_.thread_local_cluster_.lb_.host_}));
  EXPECT_CALL(*client, setPushCallbacks(_));
  EXPECT_CALL(*client, sendCommand(_));
  NiceMock<Network::MockConnection> downstream;
  auto subscriber = std::make_shared<RedisProxy::DownstreamSubscriber>(
      downstream, testDownstreamSubscriberStats());
  registry->subscribe({"ch"}, subscriber);

  // The subscription setup above is synchronous (no post), so anything captured now is the
  // rebalance.
  std::vector<Event::PostCb> posted;
  EXPECT_CALL(tls_.dispatcher_, post(_)).WillRepeatedly([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });

  // Member-update with EMPTY added/removed — exactly what a slot-only rebalance delivers.
  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, {});

  // The topology-change resubscribe is posted despite the empty host delta.
  EXPECT_EQ(1UL, posted.size());

  tls_.shutdownThread();
}

// --- read-policy placement pool-side glue (hostServesChannelSlot / read-shaped resolve) ---

// A load balancer that ALSO implements ShardMembershipResolver, so the conn pool's SHARD_MEMBERS
// callbacks (which dynamic_cast the cluster LB to that interface) can be driven against a
// controllable shard snapshot. NiceMock<MockLoadBalancer> supplies the LoadBalancer surface.
class FakeShardMembershipResolverLb : public NiceMock<Upstream::MockLoadBalancer>,
                                      public Clusters::Redis::ShardMembershipResolver {
public:
  std::optional<Clusters::Redis::ShardMembers> membersForSlot(uint64_t slot) const override {
    for (const auto& [s, members] : members_) {
      if (s == slot) {
        return members;
      }
    }
    return std::nullopt;
  }
  std::vector<std::pair<uint64_t, Clusters::Redis::ShardMembers>> members_;
};

namespace {
Upstream::HostSharedPtr makeHealthHost(Upstream::Host::Health health) {
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host, coarseHealth()).WillByDefault(Return(health));
  return host;
}
Clusters::Redis::ShardMembers shardMembers(std::vector<Upstream::HostSharedPtr> hosts) {
  return Clusters::Redis::ShardMembers{std::make_shared<Upstream::HostVector>(std::move(hosts))};
}
} // namespace

// hostServesChannelSlot under the DEFAULT (MASTER) read policy: the only valid home is the
// slot's current primary (members are primary-first), so a replica-recorded owner re-homes; a
// channel whose slot has no snapshot keeps its record (transient tolerance).
TEST_F(RedisConnPoolImplTest, HostServesChannelSlotPrimaryPolicySemantics) {
  FakeShardMembershipResolverLb fake_lb;
  ON_CALL(cm_.thread_local_cluster_, loadBalancer()).WillByDefault(ReturnRef(fake_lb));
  setup();

  auto primary = makeHealthHost(Upstream::Host::Health::Healthy);
  auto replica = makeHealthHost(Upstream::Host::Health::Healthy);
  auto stranger = makeHealthHost(Upstream::Host::Health::Healthy);
  fake_lb.members_.push_back(
      {Clusters::Redis::redisSlotForKey("chan"), shardMembers({primary, replica})});

  auto& pool = threadLocalPool();
  EXPECT_TRUE(pool.hostServesChannelSlot("chan", primary));   // the slot primary
  EXPECT_FALSE(pool.hostServesChannelSlot("chan", replica));  // MASTER homes on the primary only
  EXPECT_FALSE(pool.hostServesChannelSlot("chan", stranger)); // not a member at all
  EXPECT_TRUE(pool.hostServesChannelSlot("channel-with-no-snapshot", primary));

  tls_.shutdownThread();
}

// Under a replica-capable read policy, validity is health-agnostic shard MEMBERSHIP: both the
// primary and a replica are valid homes; a non-member is not.
TEST_F(RedisConnPoolImplTest, HostServesChannelSlotMembershipUnderReplicaPolicy) {
  FakeShardMembershipResolverLb fake_lb;
  ON_CALL(cm_.thread_local_cluster_, loadBalancer()).WillByDefault(ReturnRef(fake_lb));
  read_policy_ =
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::REPLICA;
  setup();

  auto primary = makeHealthHost(Upstream::Host::Health::Healthy);
  auto replica = makeHealthHost(Upstream::Host::Health::Healthy);
  auto stranger = makeHealthHost(Upstream::Host::Health::Healthy);
  fake_lb.members_.push_back(
      {Clusters::Redis::redisSlotForKey("chan"), shardMembers({primary, replica})});

  auto& pool = threadLocalPool();
  EXPECT_TRUE(pool.hostServesChannelSlot("chan", primary));
  EXPECT_TRUE(pool.hostServesChannelSlot("chan", replica));
  EXPECT_FALSE(pool.hostServesChannelSlot("chan", stranger));

  tls_.shutdownThread();
}

// Placement literally follows the read rule: the resolve hands the LB a READ-shaped context, so
// readPolicy()/client zone apply exactly as on the data path.
TEST_F(RedisConnPoolImplTest, ChooseUpstreamHostForChannelUsesReadShapedContext) {
  setup();
  bool saw_read_context = false;
  ON_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillByDefault(
          Invoke([&](Upstream::LoadBalancerContext* ctx) -> Upstream::HostSelectionResponse {
            auto* rctx = dynamic_cast<Clusters::Redis::RedisLoadBalancerContext*>(ctx);
            if (rctx != nullptr) {
              saw_read_context = rctx->isReadCommand();
            }
            return {cm_.thread_local_cluster_.lb_.host_};
          }));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  threadLocalPool().chooseUpstreamHostForChannel("chan");
  EXPECT_TRUE(saw_read_context);
  tls_.shutdownThread();
}

// Without a ShardMembershipResolver (a non-cluster upstream, or the cluster not yet present),
// hostServesChannelSlot must match the read-policy placement exactly: a recorded owner is valid
// iff it still matches chooseUpstreamHostForChannel's result,
// and a null (transient) resolution keeps the record. Otherwise the record would stick to its first
// host while PUBLISH re-routes as the primary moves.
TEST_F(RedisConnPoolImplTest, HostServesChannelSlotFollowsPrimaryWithoutResolver) {
  setup(); // default cm_.thread_local_cluster_.lb_ is a plain MockLoadBalancer, not a resolver
  auto host_a = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto host_b = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto& pool = threadLocalPool();

  ON_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillByDefault(
          Invoke([host_a](Upstream::LoadBalancerContext*) -> Upstream::HostSelectionResponse {
            return {host_a};
          }));
  EXPECT_TRUE(pool.hostServesChannelSlot("chan", host_a));  // matches primary
  EXPECT_FALSE(pool.hostServesChannelSlot("chan", host_b)); // primary moved

  // A transient null resolution keeps the record (PRIMARY-null parity).
  ON_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillByDefault(Invoke([](Upstream::LoadBalancerContext*) -> Upstream::HostSelectionResponse {
        return {nullptr};
      }));
  EXPECT_TRUE(pool.hostServesChannelSlot("chan", host_a));

  tls_.shutdownThread();
}

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
