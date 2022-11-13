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
#include "test/mocks/api/mocks.h"
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
             const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache = nullptr) {
    EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (cluster_exists) {
      cm_.initializeThreadLocalClusters({"fake_cluster"});
    }

    std::unique_ptr<NiceMock<Stats::MockStore>> store =
        std::make_unique<NiceMock<Stats::MockStore>>();

    upstream_cx_drained_.value_ = 0;
    ON_CALL(*store, counter(Eq("upstream_cx_drained")))
        .WillByDefault(ReturnRef(upstream_cx_drained_));
    ON_CALL(upstream_cx_drained_, value()).WillByDefault(Invoke([&]() -> uint64_t {
      return upstream_cx_drained_.value_;
    }));
    ON_CALL(upstream_cx_drained_, inc()).WillByDefault(Invoke([&]() {
      upstream_cx_drained_.value_++;
    }));

    max_upstream_unknown_connections_reached_.value_ = 0;
    ON_CALL(*store, counter(Eq("max_upstream_unknown_connections_reached")))
        .WillByDefault(ReturnRef(max_upstream_unknown_connections_reached_));
    ON_CALL(max_upstream_unknown_connections_reached_, value())
        .WillByDefault(
            Invoke([&]() -> uint64_t { return max_upstream_unknown_connections_reached_.value_; }));
    ON_CALL(max_upstream_unknown_connections_reached_, inc()).WillByDefault(Invoke([&]() {
      max_upstream_unknown_connections_reached_.value_++;
    }));

    cluster_refresh_manager_ =
        std::make_shared<NiceMock<Extensions::Common::Redis::MockClusterRefreshManager>>();
    auto redis_command_stats =
        Common::Redis::RedisCommandStats::createRedisCommandStats(store->symbolTable());
    std::shared_ptr<InstanceImpl> conn_pool_impl = std::make_shared<InstanceImpl>(
        cluster_name_, cm_, *this, tls_,
        Common::Redis::Client::createConnPoolSettings(20, hashtagging, true, max_unknown_conns,
                                                      read_policy_),
        api_, std::move(store), redis_command_stats, cluster_refresh_manager_, dns_cache);
    conn_pool_impl->init();
    // Set the authentication password for this connection pool.
    conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().auth_username_ = auth_username_;
    conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().auth_password_ = auth_password_;
    conn_pool_ = std::move(conn_pool_impl);
    test_address_ = Network::Utility::resolveUrl("tcp://127.0.0.1:3000");
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

  // Common::Redis::Client::ClientFactory
  Common::Redis::Client::ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher&,
                                          const Common::Redis::Client::Config&,
                                          const Common::Redis::RedisCommandStatsSharedPtr&,
                                          Stats::Scope&, const std::string& username,
                                          const std::string& password, bool) override {
    EXPECT_EQ(auth_username_, username);
    EXPECT_EQ(auth_password_, password);
    return Common::Redis::Client::ClientPtr{create_(host)};
  }

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
              return cm_.thread_local_cluster_.lb_.host_;
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
        return cm_.thread_local_cluster_.lb_.host_;
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
        return cm_.thread_local_cluster_.lb_.host_;
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
        return cm_.thread_local_cluster_.lb_.host_;
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
  update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);
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
  update_callbacks_->onClusterAddOrUpdate(cluster2);

  // Add the cluster back. Request to the cluster should succeed.
  update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(true, "foo", 9631199822919835226U);

  // Remove a cluster we don't care about. Request to the cluster should succeed.
  update_callbacks_->onClusterRemoval("some_other_cluster");
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(false, "foo", 9631199822919835226U);

  // Update the cluster. This should count as a remove followed by an add. Request to the cluster
  // should succeed.
  EXPECT_CALL(*client_, close());
  update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);
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
  update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);
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

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host1));
  EXPECT_CALL(*this, create_(Eq(host1))).WillOnce(Return(client1));

  Common::Redis::Client::MockPoolRequest active_request1;
  EXPECT_CALL(*host1, address()).WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client1, makeRequest_(Ref(*value), _)).WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequest("hash_key", value, callbacks, transaction_);
  EXPECT_NE(nullptr, request1);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host2));
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
  auto host1_test_address = Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
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
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(nullptr));
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
    update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);

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
  auto new_host1_test_address = Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
  auto new_host2_test_address = Network::Utility::resolveUrl("tcp://[2001:470:813b::1]:3333");
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
  auto new_host1_test_address = Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
  auto new_host2_test_address = Network::Utility::resolveUrl("tcp://[2001:470:813b::1]:3333");
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
  auto new_host1_test_address = Network::Utility::resolveUrl("tcp://10.0.0.1:3000");
  auto new_host2_test_address = Network::Utility::resolveUrl("tcp://[2001:470:813b::1]:3333");
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

  absl::optional<envoy::config::cluster::v3::Cluster::CustomClusterType> cluster_type;
  cluster_type.emplace();
  cluster_type->set_name("envoy.clusters.redis");
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, clusterType())
      .WillOnce(ReturnRef(cluster_type));
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, lbType())
      .WillOnce(Return(Upstream::LoadBalancerType::ClusterProvided));

  setup();

  makeSimpleRequest(true, "foo", 44950);

  makeSimpleRequest(false, "bar", 37829);

  EXPECT_CALL(*client_, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, MakeRequestToRedisClusterHashtag) {

  absl::optional<envoy::config::cluster::v3::Cluster::CustomClusterType> cluster_type;
  cluster_type.emplace();
  cluster_type->set_name("envoy.clusters.redis");
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, clusterType())
      .WillOnce(ReturnRef(cluster_type));
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, lbType())
      .WillOnce(Return(Upstream::LoadBalancerType::ClusterProvided));

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
  host_info->address_ = Network::Utility::parseInternetAddress("1.2.3.4", 6379);
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
            nullptr, absl::nullopt};
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
  absl::optional<std::reference_wrapper<
      Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks>>
      saved_callbacks;

  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks& callbacks) {
        saved_callbacks = callbacks;
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, absl::nullopt};
      }));

  client->client_callbacks_.back()->onRedirection(std::move(moved_response), "foo:6379", false);

  EXPECT_CALL(*handle, onDestroy());

  auto host_info = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
  host_info->address_ = Network::Utility::parseInternetAddress("1.2.3.4", 6379);
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
  absl::optional<std::reference_wrapper<
      Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks>>
      saved_callbacks;

  EXPECT_CALL(*dns_cache, loadDnsCacheEntry_(Eq("foo:6379"), 6379, false, _))
      .WillOnce(Invoke([&](absl::string_view, uint16_t, bool,
                           Extensions::Common::DynamicForwardProxy::DnsCache::
                               LoadDnsCacheEntryCallbacks& callbacks) {
        saved_callbacks = callbacks;
        return Extensions::Common::DynamicForwardProxy::MockDnsCache::MockLoadDnsCacheEntryResult{
            Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading,
            handle, absl::nullopt};
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
  std::unique_ptr<NiceMock<Stats::MockStore>> store =
      std::make_unique<NiceMock<Stats::MockStore>>();
  cluster_refresh_manager_ =
      std::make_shared<NiceMock<Extensions::Common::Redis::MockClusterRefreshManager>>();
  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(store->symbolTable());
  conn_pool_ = std::make_shared<InstanceImpl>(
      cluster_name_, cm_, *this, tls_,
      Common::Redis::Client::createConnPoolSettings(20, true, true, 100, read_policy_), api_,
      std::move(store), redis_command_stats, cluster_refresh_manager_, nullptr);
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

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
