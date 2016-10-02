#include "common/upstream/logical_dns_cluster.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;

namespace Upstream {

class LogicalDnsClusterTest : public testing::Test {
public:
  void setup(const std::string& json) {
    Json::StringLoader config(json);
    resolve_timer_ = new Event::MockTimer(&dns_resolver_.dispatcher_);
    cluster_.reset(new LogicalDnsCluster(config, runtime_, stats_store_, ssl_context_manager_,
                                         dns_resolver_, tls_));
    cluster_->addMemberUpdateCb([&](const std::vector<HostPtr>&, const std::vector<HostPtr>&)
                                    -> void { membership_updated_.ready(); });
    cluster_->setInitializedCb([&]() -> void { initialized_.ready(); });
  }

  void expectResolve() {
    EXPECT_CALL(dns_resolver_, resolve("foo.bar.com", _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsResolver::ResolveCb cb)
                             -> void { dns_callback_ = cb; }));
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<Network::MockDnsResolver> dns_resolver_;
  Network::DnsResolver::ResolveCb dns_callback_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  std::unique_ptr<LogicalDnsCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(LogicalDnsClusterTest, BadConfig) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}, {"url": "tcp://foo2.bar.com:443"}]
  }
  )EOF";

  EXPECT_THROW(setup(json), EnvoyException);
}

TEST_F(LogicalDnsClusterTest, Basic) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}],
    "dns_refresh_rate_ms": 4000
  }
  )EOF";

  expectResolve();
  setup(json);

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000)));
  dns_callback_({"127.0.0.1", "127.0.0.2"});

  EXPECT_EQ(1UL, cluster_->hosts().size());
  EXPECT_EQ(1UL, cluster_->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->localZoneHosts().size());
  EXPECT_EQ(0UL, cluster_->localZoneHealthyHosts().size());
  EXPECT_EQ(cluster_->hosts()[0], cluster_->healthyHosts()[0]);
  HostPtr logical_host = cluster_->hosts()[0];

  EXPECT_CALL(dns_resolver_.dispatcher_, createClientConnection_("tcp://127.0.0.1:443"));
  logical_host->createConnection(dns_resolver_.dispatcher_);
  logical_host->outlierDetector().putHttpResponseCode(200);

  expectResolve();
  resolve_timer_->callback_();

  // Should not cause any changes.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_({"127.0.0.1", "127.0.0.2", "127.0.0.3"});

  EXPECT_EQ(logical_host, cluster_->hosts()[0]);
  EXPECT_CALL(dns_resolver_.dispatcher_, createClientConnection_("tcp://127.0.0.1:443"));
  Host::CreateConnectionData data = logical_host->createConnection(dns_resolver_.dispatcher_);
  EXPECT_FALSE(data.host_description_->canary());
  EXPECT_EQ(&cluster_->hosts()[0]->cluster(), &data.host_description_->cluster());
  EXPECT_EQ(&cluster_->hosts()[0]->stats(), &data.host_description_->stats());
  EXPECT_EQ("tcp://127.0.0.1:443", data.host_description_->url());
  EXPECT_EQ("", data.host_description_->zone());
  data.host_description_->outlierDetector().putHttpResponseCode(200);

  expectResolve();
  resolve_timer_->callback_();

  // Should cause a change.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_({"127.0.0.3", "127.0.0.1", "127.0.0.2"});

  EXPECT_EQ(logical_host, cluster_->hosts()[0]);
  EXPECT_CALL(dns_resolver_.dispatcher_, createClientConnection_("tcp://127.0.0.3:443"));
  logical_host->createConnection(dns_resolver_.dispatcher_);

  expectResolve();
  resolve_timer_->callback_();

  // Empty should not cause any change.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_({});

  EXPECT_EQ(logical_host, cluster_->hosts()[0]);
  EXPECT_CALL(dns_resolver_.dispatcher_, createClientConnection_("tcp://127.0.0.3:443"));
  logical_host->createConnection(dns_resolver_.dispatcher_);

  cluster_->shutdown();
  tls_.shutdownThread();
}

} // Upstream
