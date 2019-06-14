#include <memory>

#include "common/network/utility.h"
#include "common/stats/fake_symbol_table_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/filters/network/common/redis/utility.h"
#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include "test/extensions/clusters/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/printers.h"

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
  void setup(bool cluster_exists = true, bool hashtagging = true) {
    EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (!cluster_exists) {
      EXPECT_CALL(cm_, get(Eq("fake_cluster"))).WillOnce(Return(nullptr));
    }

    std::unique_ptr<InstanceImpl> conn_pool_impl = std::make_unique<InstanceImpl>(
        cluster_name_, cm_, *this, tls_,
        Common::Redis::Client::createConnPoolSettings(20, hashtagging, true), api_, *symbol_table_);
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
    if (create_client && !auth_password_.empty()) {
      EXPECT_CALL(*client_, makeRequest(_, _)).WillOnce(Return(nullptr));
    }
    EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(test_address_));
    EXPECT_CALL(*client_, makeRequest(Ref(value), Ref(callbacks)))
        .WillOnce(Return(&active_request));
    Common::Redis::Client::PoolRequest* request =
        conn_pool_->makeRequest(hash_key, value, callbacks);
    EXPECT_EQ(&active_request, request);
  }

  // Common::Redis::Client::ClientFactory
  Common::Redis::Client::ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher&,
                                          const Common::Redis::Client::Config&) override {
    return Common::Redis::Client::ClientPtr{create_(host)};
  }

  MOCK_METHOD1(create_, Common::Redis::Client::Client*(Upstream::HostConstSharedPtr host));

  const std::string cluster_name_{"fake_cluster"};
  Envoy::Test::Global<Stats::FakeSymbolTableImpl> symbol_table_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  InstanceSharedPtr conn_pool_;
  Upstream::ClusterUpdateCallbacks* update_callbacks_{};
  Common::Redis::Client::MockClient* client_{};
  Network::Address::InstanceConstSharedPtr test_address_;
  std::string auth_password_;
  NiceMock<Api::MockApi> api_;
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

TEST_F(RedisConnPoolImplTest, BasicWithAuthPassword) {
  InSequence s;

  auth_password_ = "testing password";
  setup();

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest auth_request, active_request;
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
  EXPECT_CALL(
      *client,
      makeRequest(Eq(NetworkFilters::Common::Redis::Utility::makeAuthCommand(auth_password_)), _))
      .WillOnce(Return(&auth_request));
  EXPECT_CALL(*cm_.thread_local_cluster_.lb_.host_, address())
      .WillRepeatedly(Return(test_address_));
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  Common::Redis::Client::PoolRequest* request =
      conn_pool_->makeRequest("hash_key", value, callbacks);
  EXPECT_EQ(&active_request, request);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
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

// Conn pool created when no cluster exists at creation time. Dynamic cluster creation and removal
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

TEST_F(RedisConnPoolImplTest, makeRequestToHost) {
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

TEST_F(RedisConnPoolImplTest, makeRequestToHostWithAuthPassword) {
  InSequence s;

  auth_password_ = "superduperpassword";
  setup(false);

  Common::Redis::RespValue value;
  Common::Redis::Client::MockPoolRequest auth_request1, active_request1;
  Common::Redis::Client::MockPoolRequest auth_request2, active_request2;
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
  EXPECT_CALL(
      *client1,
      makeRequest(Eq(NetworkFilters::Common::Redis::Utility::makeAuthCommand(auth_password_)), _))
      .WillOnce(Return(&auth_request1));
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
  EXPECT_CALL(
      *client2,
      makeRequest(Eq(NetworkFilters::Common::Redis::Utility::makeAuthCommand(auth_password_)), _))
      .WillOnce(Return(&auth_request2));
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks2)))
      .WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 =
      conn_pool_->makeRequestToHost("2001:470:813B:0:0:0:0:1:3333", value, callbacks2);
  EXPECT_EQ(&active_request2, request2);
  EXPECT_EQ(host2->address()->asString(), "[2001:470:813b::1]:3333");

  EXPECT_CALL(*client2, close());
  EXPECT_CALL(*client1, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, MakeRequestToRedisCluster) {

  absl::optional<envoy::api::v2::Cluster::CustomClusterType> cluster_type;
  cluster_type.emplace();
  cluster_type->set_name("envoy.clusters.redis");
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, clusterType())
      .Times(2)
      .WillRepeatedly(ReturnRef(cluster_type));
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, lbType())
      .Times(2)
      .WillRepeatedly(Return(Upstream::LoadBalancerType::ClusterProvided));

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
      .Times(2)
      .WillRepeatedly(ReturnRef(cluster_type));
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, lbType())
      .Times(2)
      .WillRepeatedly(Return(Upstream::LoadBalancerType::ClusterProvided));

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
