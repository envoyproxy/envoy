#include <memory>
#include <string>

#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
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
  void setup(bool cluster_exists = true) {
    EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (!cluster_exists) {
      EXPECT_CALL(cm_, get("fake_cluster")).WillOnce(Return(nullptr));
    }
    conn_pool_ = std::make_unique<InstanceImpl>(cluster_name_, cm_, *this, tls_,
                                                Common::Redis::Client::createConnPoolSettings());
  }

  void makeSimpleRequest(bool create_client) {
    EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_));
    if (create_client) {
      client_ = new NiceMock<Common::Redis::Client::MockClient>();
      EXPECT_CALL(*this, create_(_)).WillOnce(Return(client_));
    }
    Common::Redis::RespValue value;
    Common::Redis::Client::MockPoolCallbacks callbacks;
    Common::Redis::Client::MockPoolRequest active_request;
    EXPECT_CALL(*client_, makeRequest(Ref(value), Ref(callbacks)))
        .WillOnce(Return(&active_request));
    Common::Redis::Client::PoolRequest* request =
        conn_pool_->makeRequest("hash_key", value, callbacks);
    EXPECT_EQ(&active_request, request);
  }

  // Common::Redis::Client::ClientFactory
  Common::Redis::Client::ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher&,
                                          const Common::Redis::Client::Config&) override {
    return Common::Redis::Client::ClientPtr{create_(host)};
  }

  MOCK_METHOD1(create_, Common::Redis::Client::Client*(Upstream::HostConstSharedPtr host));

  const std::string cluster_name_{"fake_cluster"};
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  InstancePtr conn_pool_;
  Upstream::ClusterUpdateCallbacks* update_callbacks_{};
  Common::Redis::Client::MockClient* client_{};
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
  makeSimpleRequest(true);

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
  makeSimpleRequest(true);

  // Remove a cluster we don't care about. Request to the cluster should succeed.
  update_callbacks_->onClusterRemoval("some_other_cluster");
  makeSimpleRequest(false);

  // Update the cluster. This should count as a remove followed by an add. Request to the cluster
  // should succeed.
  EXPECT_CALL(*client_, close());
  update_callbacks_->onClusterAddOrUpdate(cm_.thread_local_cluster_);
  makeSimpleRequest(true);

  // Remove the cluster to make sure we safely destruct with no cluster.
  EXPECT_CALL(*client_, close());
  update_callbacks_->onClusterRemoval("fake_cluster");
}

TEST_F(RedisConnPoolImplTest, HostRemove) {
  InSequence s;

  setup();

  Common::Redis::Client::MockPoolCallbacks callbacks;
  Common::Redis::RespValue value;
  std::shared_ptr<Upstream::Host> host1(new Upstream::MockHost());
  std::shared_ptr<Upstream::Host> host2(new Upstream::MockHost());
  Common::Redis::Client::MockClient* client1 = new NiceMock<Common::Redis::Client::MockClient>();
  Common::Redis::Client::MockClient* client2 = new NiceMock<Common::Redis::Client::MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host1));
  EXPECT_CALL(*this, create_(Eq(host1))).WillOnce(Return(client1));

  Common::Redis::Client::MockPoolRequest active_request1;
  EXPECT_CALL(*client1, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request1));
  Common::Redis::Client::PoolRequest* request1 =
      conn_pool_->makeRequest("hash_key", value, callbacks);
  EXPECT_EQ(&active_request1, request1);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host2));
  EXPECT_CALL(*this, create_(Eq(host2))).WillOnce(Return(client2));

  Common::Redis::Client::MockPoolRequest active_request2;
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request2));
  Common::Redis::Client::PoolRequest* request2 = conn_pool_->makeRequest("bar", value, callbacks);
  EXPECT_EQ(&active_request2, request2);

  EXPECT_CALL(*client2, close());
  cm_.thread_local_cluster_.cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, {host2});

  EXPECT_CALL(*client1, close());
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
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  conn_pool_->makeRequest("hash_key", value, callbacks);

  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  client->runHighWatermarkCallbacks();
  client->runLowWatermarkCallbacks();
  client->raiseEvent(Network::ConnectionEvent::RemoteClose);

  tls_.shutdownThread();
}

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
