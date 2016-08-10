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
using testing::ReturnNew;
using testing::SaveArg;

namespace Upstream {

class ClusterManagerImplForTest : public ClusterManagerImpl {
public:
  using ClusterManagerImpl::ClusterManagerImpl;

  Http::ConnectionPool::InstancePtr allocateConnPool(Event::Dispatcher&, ConstHostPtr host,
                                                     Stats::Store&) override {
    return Http::ConnectionPool::InstancePtr{allocateConnPool_(host)};
  }

  MOCK_METHOD1(allocateConnPool_, Http::ConnectionPool::Instance*(ConstHostPtr host));
};

TEST(ClusterManagerImplTest, DynamicHostRemove) {
  std::string json = R"EOF(
  {
    "cluster_manager": {
      "clusters": [
      {
        "name": "cluster_1",
        "connect_timeout_ms": 250,
        "type": "strict_dns",
        "lb_type": "round_robin",
        "hosts": [{"url": "tcp://localhost:11001"}]
      }]
    }
  }
  )EOF";

  Json::StringLoader loader(json);

  Stats::IsolatedStoreImpl stats;
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Network::MockDnsResolver> dns_resolver;
  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&dns_resolver.dispatcher_);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Runtime::MockRandomGenerator> random;
  Ssl::ContextManagerImpl ssl_context_manager(runtime);
  EXPECT_CALL(dns_resolver, resolve(_, _)).WillRepeatedly(SaveArg<1>(&dns_callback));

  ClusterManagerImplForTest cluster_manager(loader.getObject("cluster_manager"), stats, tls,
                                            dns_resolver, ssl_context_manager, runtime, random,
                                            "us-east-1d");

  // Test for no hosts returning the correct values before we have hosts.
  EXPECT_EQ(nullptr, cluster_manager.httpConnPoolForCluster("cluster_1"));
  EXPECT_EQ(nullptr, cluster_manager.tcpConnForCluster("cluster_1").connection_);
  EXPECT_EQ(2UL, stats.counter("cluster.cluster_1.upstream_cx_none_healthy").value());

  // Set up for an initialize callback.
  ReadyWatcher initialized;
  cluster_manager.setInitializedCb([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());

  dns_callback({"127.0.0.1", "127.0.0.2"});

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager.setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(cluster_manager, allocateConnPool_(_))
      .Times(2)
      .WillRepeatedly(ReturnNew<Http::ConnectionPool::MockInstance>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager.httpConnPoolForCluster("cluster_1"));
  Http::ConnectionPool::MockInstance* cp2 = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager.httpConnPoolForCluster("cluster_1"));

  EXPECT_NE(cp1, cp2);

  Http::ConnectionPool::Instance::DrainedCb drained_cb;
  EXPECT_CALL(*cp1, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb));

  // Remove the first host, this should lead to the first cp being drained.
  dns_timer_->callback_();
  dns_callback({"127.0.0.2"});
  drained_cb();
  drained_cb = nullptr;

  // Make sure we get back the same connection pool for the 2nd host as we did before the change.
  Http::ConnectionPool::MockInstance* cp3 = dynamic_cast<Http::ConnectionPool::MockInstance*>(
      cluster_manager.httpConnPoolForCluster("cluster_1"));
  EXPECT_EQ(cp2, cp3);

  // Now add and remove a host that we never have a conn pool to. This should not lead to any
  // drain callbacks, etc.
  dns_timer_->callback_();
  dns_callback({"127.0.0.2", "127.0.0.3"});
  dns_timer_->callback_();
  dns_callback({"127.0.0.2"});
}

} // Upstream
