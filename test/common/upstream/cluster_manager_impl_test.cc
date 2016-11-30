#include "envoy/upstream/upstream.h"

#include "common/ssl/context_manager_impl.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::SaveArg;

namespace Upstream {

class ClusterManagerImplForTest : public ClusterManagerImpl {
public:
  using ClusterManagerImpl::ClusterManagerImpl;

  Http::ConnectionPool::InstancePtr allocateConnPool(Event::Dispatcher&, ConstHostPtr host,
                                                     Stats::Store&, ResourcePriority) override {
    return Http::ConnectionPool::InstancePtr{allocateConnPool_(host)};
  }

  MOCK_METHOD1(allocateConnPool_, Http::ConnectionPool::Instance*(ConstHostPtr host));
};

class ClusterManagerImplTest : public testing::Test {
public:
  void create(const Json::Object& config) {
    cluster_manager_.reset(new ClusterManagerImplForTest(config, stats_, tls_, dns_resolver_,
                                                         ssl_context_manager_, runtime_, random_,
                                                         "us-east-1d", "local_address"));
  }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Network::MockDnsResolver> dns_resolver_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Ssl::ContextManagerImpl ssl_context_manager_{runtime_};
  std::unique_ptr<ClusterManagerImplForTest> cluster_manager_;
};

TEST_F(ClusterManagerImplTest, NoSdsConfig) {
  std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "sds",
      "lb_type": "round_robin"
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  EXPECT_THROW(create(*loader), EnvoyException);
}

TEST_F(ClusterManagerImplTest, UnknownClusterType) {
  std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "foo",
      "lb_type": "round_robin"
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  EXPECT_THROW(create(*loader), EnvoyException);
}

TEST_F(ClusterManagerImplTest, LocalClusterNotDefined) {
  std::string json = R"EOF(
  {
    "local_cluster_name": "new_cluster",
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    },
    {
      "name": "cluster_2",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11002"}]
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  EXPECT_THROW(create(*loader), EnvoyException);
}

TEST_F(ClusterManagerImplTest, LocalClusterDefined) {
  std::string json = R"EOF(
  {
    "local_cluster_name": "new_cluster",
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    },
    {
      "name": "cluster_2",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11002"}]
    },
    {
      "name": "new_cluster",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11002"}]
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  create(*loader);
}

TEST_F(ClusterManagerImplTest, DuplicateCluster) {
  std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    },
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  EXPECT_THROW(create(*loader), EnvoyException);
}

TEST_F(ClusterManagerImplTest, UnknownHcType) {
  std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}],
      "health_check": {
        "type": "foo"
      }
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  EXPECT_THROW(create(*loader), EnvoyException);
}

TEST_F(ClusterManagerImplTest, TcpHealthChecker) {
  std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}],
      "health_check": {
        "type": "tcp",
        "timeout_ms": 1000,
        "interval_ms": 1000,
        "unhealthy_threshold": 2,
        "healthy_threshold": 2,
        "send": [
          {"binary": "01"}
        ],
        "receive": [
          {"binary": "02"}
        ]
      }
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(dns_resolver_.dispatcher_, createClientConnection_("tcp://127.0.0.1:11001"))
      .WillOnce(Return(connection));
  create(*loader);
}

TEST_F(ClusterManagerImplTest, UnknownCluster) {
  std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  create(*loader);
  EXPECT_EQ(nullptr, cluster_manager_->get("hello"));
  EXPECT_THROW(cluster_manager_->httpConnPoolForCluster("hello", ResourcePriority::Default),
               EnvoyException);
  EXPECT_THROW(cluster_manager_->tcpConnForCluster("hello"), EnvoyException);
  EXPECT_THROW(cluster_manager_->httpAsyncClientForCluster("hello"), EnvoyException);
}

TEST_F(ClusterManagerImplTest, DynamicHostRemove) {
  std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "strict_dns",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://localhost:11001"}]
    }]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&dns_resolver_.dispatcher_);
  EXPECT_CALL(dns_resolver_, resolve(_, _)).WillRepeatedly(SaveArg<1>(&dns_callback));
  create(*loader);

  // Test for no hosts returning the correct values before we have hosts.
  EXPECT_EQ(nullptr,
            cluster_manager_->httpConnPoolForCluster("cluster_1", ResourcePriority::Default));
  EXPECT_EQ(nullptr, cluster_manager_->tcpConnForCluster("cluster_1").connection_);
  EXPECT_EQ(2UL, stats_.counter("cluster.cluster_1.upstream_cx_none_healthy").value());

  // Set up for an initialize callback.
  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());

  dns_callback({"127.0.0.1", "127.0.0.2"});

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(*cluster_manager_, allocateConnPool_(_))
      .Times(4)
      .WillRepeatedly(ReturnNew<Http::ConnectionPool::MockInstance>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager_->httpConnPoolForCluster("cluster_1", ResourcePriority::Default));
  Http::ConnectionPool::MockInstance* cp2 = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager_->httpConnPoolForCluster("cluster_1", ResourcePriority::Default));
  Http::ConnectionPool::MockInstance* cp1_high = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager_->httpConnPoolForCluster("cluster_1", ResourcePriority::High));
  Http::ConnectionPool::MockInstance* cp2_high = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager_->httpConnPoolForCluster("cluster_1", ResourcePriority::High));

  EXPECT_NE(cp1, cp2);
  EXPECT_NE(cp1_high, cp2_high);
  EXPECT_NE(cp1, cp1_high);

  Http::ConnectionPool::Instance::DrainedCb drained_cb;
  EXPECT_CALL(*cp1, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb));
  Http::ConnectionPool::Instance::DrainedCb drained_cb_high;
  EXPECT_CALL(*cp1_high, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb_high));

  // Remove the first host, this should lead to the first cp being drained.
  dns_timer_->callback_();
  dns_callback({"127.0.0.2"});
  drained_cb();
  drained_cb = nullptr;
  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_)).Times(2);
  drained_cb_high();
  drained_cb_high = nullptr;

  // Make sure we get back the same connection pool for the 2nd host as we did before the change.
  Http::ConnectionPool::MockInstance* cp3 = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager_->httpConnPoolForCluster("cluster_1", ResourcePriority::Default));
  Http::ConnectionPool::MockInstance* cp3_high = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager_->httpConnPoolForCluster("cluster_1", ResourcePriority::High));
  EXPECT_EQ(cp2, cp3);
  EXPECT_EQ(cp2_high, cp3_high);

  // Now add and remove a host that we never have a conn pool to. This should not lead to any
  // drain callbacks, etc.
  dns_timer_->callback_();
  dns_callback({"127.0.0.2", "127.0.0.3"});
  dns_timer_->callback_();
  dns_callback({"127.0.0.2"});
}

} // Upstream
