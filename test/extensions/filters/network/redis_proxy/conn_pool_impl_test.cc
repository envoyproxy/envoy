#include <memory>

#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/filters/network/common/redis/utility.h"
#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include "test/extensions/clusters/redis/mocks.h"
#include "test/extensions/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/thread_local/mocks.h"

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
  void setup(bool cluster_exists = true, bool hashtagging = true,
             uint32_t max_unknown_conns = 100) {
    EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (!cluster_exists) {
      EXPECT_CALL(cm_, get(Eq("fake_cluster"))).WillOnce(Return(nullptr));
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

    redirection_mgr_ =
        std::make_shared<NiceMock<Extensions::Common::Redis::MockRedirectionManager>>();
    auto redis_command_stats =
        Common::Redis::RedisCommandStats::createRedisCommandStats(store->symbolTable());
    std::unique_ptr<InstanceImpl> conn_pool_impl = std::make_unique<InstanceImpl>(
        cluster_name_, cm_, *this, tls_,
        Common::Redis::Client::createConnPoolSettings(20, hashtagging, true, max_unknown_conns,
                                                      read_policy_),
        api_, std::move(store), redis_command_stats, redirection_mgr_);
    // Set the authentication password for this connection pool.
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
    Common::Redis::RespValue value;
    Common::Redis::Client::MockPoolCallbacks callbacks;
    Common::Redis::Client::MockPoolRequest active_request;
    EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(test_address_));
    EXPECT_CALL(*client_, makeRequest(Ref(value), Ref(callbacks)))
        .WillOnce(Return(&active_request));
    Common::Redis::Client::PoolRequest* request =
        conn_pool_->makeRequest(hash_key, value, callbacks);
    EXPECT_EQ(&active_request, request);
  }

  std::unordered_map<Upstream::HostConstSharedPtr, InstanceImpl::ThreadLocalActiveClientPtr>&
  clientMap() {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().client_map_;
  }

  InstanceImpl::ThreadLocalActiveClient* clientMap(Upstream::HostConstSharedPtr host) {
    InstanceImpl* conn_pool_impl = dynamic_cast<InstanceImpl*>(conn_pool_.get());
    return conn_pool_impl->tls_->getTyped<InstanceImpl::ThreadLocalPool>().client_map_[host].get();
  }

  std::unordered_map<std::string, Upstream::HostConstSharedPtr>& hostAddressMap() {
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
                                          Stats::Scope&, const std::string& password) override {
    EXPECT_EQ(auth_password_, password);
    return Common::Redis::Client::ClientPtr{create_(host)};
  }

  void testReadPolicy(
      envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings::ReadPolicy
          read_policy,
      NetworkFilters::Common::Redis::Client::ReadPolicy expected_read_policy) {
    InSequence s;

    read_policy_ = read_policy;
    setup();

    Common::Redis::RespValue value;
    Common::Redis::Client::MockPoolRequest auth_request, active_request, readonly_request;
    Common::Redis::Client::MockPoolCallbacks callbacks;
    Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

    EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
        .WillOnce(
            Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
              EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2_64("hash_key"));
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
    EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
    Common::Redis::Client::PoolRequest* request =
        conn_pool_->makeRequest("hash_key", value, callbacks);
    EXPECT_EQ(&active_request, request);

    EXPECT_CALL(*client, close());
    tls_.shutdownThread();
  }

  MOCK_METHOD1(create_, Common::Redis::Client::Client*(Upstream::HostConstSharedPtr host));

  const std::string cluster_name_{"fake_cluster"};
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  InstanceSharedPtr conn_pool_;
  Upstream::ClusterUpdateCallbacks* update_callbacks_{};
  Common::Redis::Client::MockClient* client_{};
  Network::Address::InstanceConstSharedPtr test_address_;
  std::string auth_password_;
  NiceMock<Api::MockApi> api_;
  envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings::ReadPolicy
      read_policy_ = envoy::config::filter::network::redis_proxy::v2::
          RedisProxy_ConnPoolSettings_ReadPolicy_MASTER;
  NiceMock<Stats::MockCounter> upstream_cx_drained_;
  NiceMock<Stats::MockCounter> max_upstream_unknown_connections_reached_;
  std::shared_ptr<NiceMock<Extensions::Common::Redis::MockRedirectionManager>> redirection_mgr_;
};

TEST_F(RedisConnPoolImplTest, Basic) {
  InSequence s;

  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest active_request;
  Common::Redis::Client::MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([&](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2_64("hash_key"));
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->downstreamConnection(), nullptr);
        return cm_.thread_local_cluster_.lb_.host_;
      }));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks);
  EXPECT_EQ(&active_request, request);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, BasicWithReadPolicy) {
  testReadPolicy(envoy::config::filter::network::redis_proxy::v2::
                     RedisProxy_ConnPoolSettings_ReadPolicy_PREFER_MASTER,
                 NetworkFilters::Common::Redis::Client::ReadPolicy::PreferMaster);
  testReadPolicy(envoy::config::filter::network::redis_proxy::v2::
                     RedisProxy_ConnPoolSettings_ReadPolicy_REPLICA,
                 NetworkFilters::Common::Redis::Client::ReadPolicy::Replica);
  testReadPolicy(envoy::config::filter::network::redis_proxy::v2::
                     RedisProxy_ConnPoolSettings_ReadPolicy_PREFER_REPLICA,
                 NetworkFilters::Common::Redis::Client::ReadPolicy::PreferReplica);
  testReadPolicy(
      envoy::config::filter::network::redis_proxy::v2::RedisProxy_ConnPoolSettings_ReadPolicy_ANY,
      NetworkFilters::Common::Redis::Client::ReadPolicy::Any);
};

TEST_F(RedisConnPoolImplTest, Hashtagging) {
  InSequence s;

  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  auto expectHashKey = [](const std::string& s) {
    return [s](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
      EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2_64(s));
      return nullptr;
    };
  };

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Invoke(expectHashKey("foo")));
  conn_pool_->makeRequest("{foo}.bar", value, callbacks);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{}{bar}")));
  conn_pool_->makeRequest("foo{}{bar}", value, callbacks);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Invoke(expectHashKey("{bar")));
  conn_pool_->makeRequest("foo{{bar}}zap", value, callbacks);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Invoke(expectHashKey("bar")));
  conn_pool_->makeRequest("foo{bar}{zap}", value, callbacks);

  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, HashtaggingNotEnabled) {
  InSequence s;

  setup(true, false); // Test with hashtagging not enabled.

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;

  auto expectHashKey = [](const std::string& s) {
    return [s](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
      EXPECT_EQ(context->computeHashKey().value(), MurmurHash::murmurHash2_64(s));
      return nullptr;
    };
  };

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("{foo}.bar")));
  conn_pool_->makeRequest("{foo}.bar", value, callbacks);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{}{bar}")));
  conn_pool_->makeRequest("foo{}{bar}", value, callbacks);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{{bar}}zap")));
  conn_pool_->makeRequest("foo{{bar}}zap", value, callbacks);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke(expectHashKey("foo{bar}{zap}")));
  conn_pool_->makeRequest("foo{bar}{zap}", value, callbacks);

  tls_.shutdownThread();
};

// ConnPool created when no cluster exists at creation time. Dynamic cluster creation and removal
// work correctly.
TEST_F(RedisConnPoolImplTest, NoClusterAtConstruction) {
  InSequence s;

  setup(false);

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks);
  EXPECT_EQ(nullptr, request);

  // Now add the cluster. Request to the cluster should succeed.
  update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);
  // MurmurHash of "foo" is 9631199822919835226U
  makeSimpleRequest(true, "foo", 9631199822919835226U);

  // Remove the cluster. Request to the cluster should fail.
  EXPECT_CALL(*client_, close());
  update_callbacks_->onClusterRemoval("fake_cluster");
  request = conn_pool_->makeRequest("hash_key", value, callbacks);
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

// This test removes a single host from the ConnPool after learning about 2 hosts from the
// associated load balancer.
TEST_F(RedisConnPoolImplTest, HostRemove) {
  InSequence s;

  setup();

  Common::Redis::Client::MockPoolCallbacks callbacks;
  Common::Redis::RespValue value;
  std::shared_ptr<Upstream::MockHost> host1(new Upstream::MockHost());
  std::shared_ptr<Upstream::MockHost> host2(new Upstream::MockHost());
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host1));
  EXPECT_CALL(*this, create_(Eq(host1))).WillOnce(Return(client1));

  Common::Redis::Client::MockPoolRequest active_request1;
  EXPECT_CALL(*host1, address()).WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client1, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequest("hash_key", value, callbacks);
  EXPECT_EQ(&active_request1, request1);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host2));
  EXPECT_CALL(*this, create_(Eq(host2))).WillOnce(Return(client2));

  Common::Redis::Client::MockPoolRequest active_request2;
  EXPECT_CALL(*host2, address()).WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 = conn_pool_->makeRequest("bar", value, callbacks);
  EXPECT_EQ(&active_request2, request2);

  EXPECT_CALL(*client2, close());
  EXPECT_CALL(*host2, address()).WillRepeatedly(Return(test_address_));
  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, {host2});

  EXPECT_CALL(*client1, close());
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

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(nullptr));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks);
  EXPECT_EQ(nullptr, request);

  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, RemoteClose) {
  InSequence s;

  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest active_request;
  Common::Redis::Client::MockPoolCallbacks callbacks;
  Common::Redis::Client::MockClient* client = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_));
  EXPECT_CALL(*this, create_(_)).WillOnce(Return(client));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  conn_pool_->makeRequest("hash_key", value, callbacks);

  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  client->runHighWatermarkCallbacks();
  client->runLowWatermarkCallbacks();
  client->raiseEvent(Network::ConnectionEvent::RemoteClose);

  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MakeRequestToHost) {
  InSequence s;

  setup(false);

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest active_request1;
  Common::Redis::Client::MockPoolRequest active_request2;
  Common::Redis::Client::MockPoolCallbacks callbacks1;
  Common::Redis::Client::MockPoolCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  // There is no cluster yet, so makeRequestToHost() should fail.
  EXPECT_EQ(nullptr, conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1));
  // Add the cluster now.
  update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
  EXPECT_CALL(*client1, makeRequest(Ref(value), Ref(callbacks1)))
      .WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
  EXPECT_EQ(&active_request1, request1);
  EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

  // IPv6 address returned from Redis server will not have square brackets
  // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
  // the address.
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks2)))
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

  EXPECT_CALL(*client2, close());
  EXPECT_CALL(*client1, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MakeRequestToHostWithZeroMaxUnknownUpstreamConnectionLimit) {
  InSequence s;

  // Create a ConnPool with a max_upstream_unknown_connections setting of 0.
  setup(true, true, 0);

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolCallbacks callbacks1;

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
  Common::Redis::Client::MockPoolCallbacks callbacks1;
  Common::Redis::Client::MockPoolCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
  EXPECT_CALL(*client1, makeRequest(Ref(value), Ref(callbacks1)))
      .WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
  EXPECT_EQ(&active_request1, request1);
  EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

  // IPv6 address returned from Redis server will not have square brackets
  // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
  // the address.
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks2)))
      .WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
  EXPECT_EQ(&active_request2, request2);
  EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

  std::unordered_map<std::string, Upstream::HostConstSharedPtr>& host_address_map =
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
  Common::Redis::Client::MockPoolCallbacks callbacks1;
  Common::Redis::Client::MockPoolCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
  EXPECT_CALL(*client1, makeRequest(Ref(value), Ref(callbacks1)))
      .WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
  EXPECT_EQ(&active_request1, request1);
  EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

  // IPv6 address returned from Redis server will not have square brackets
  // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
  // the address.
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks2)))
      .WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
  EXPECT_EQ(&active_request2, request2);
  EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

  std::unordered_map<std::string, Upstream::HostConstSharedPtr>& host_address_map =
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
  Common::Redis::Client::MockPoolCallbacks callbacks1;
  Common::Redis::Client::MockPoolCallbacks callbacks2;
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();
  Upstream::HostConstSharedPtr host1;
  Upstream::HostConstSharedPtr host2;

  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host1), Return(client1)));
  EXPECT_CALL(*client1, makeRequest(Ref(value), Ref(callbacks1)))
      .WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequestToHost("10.0.0.1:3000", value, callbacks1);
  EXPECT_EQ(&active_request1, request1);
  EXPECT_EQ(host1->address()->asString(), "10.0.0.1:3000");

  // IPv6 address returned from Redis server will not have square brackets
  // around it, while Envoy represents Address::Ipv6Instance addresses with square brackets around
  // the address.
  EXPECT_CALL(*this, create_(_)).WillOnce(DoAll(SaveArg<0>(&host2), Return(client2)));
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks2)))
      .WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
  EXPECT_EQ(&active_request2, request2);
  EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

  std::unordered_map<std::string, Upstream::HostConstSharedPtr>& host_address_map =
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

  absl::optional<envoy::api::v2::Cluster::CustomClusterType> cluster_type;
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

  absl::optional<envoy::api::v2::Cluster::CustomClusterType> cluster_type;
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

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
