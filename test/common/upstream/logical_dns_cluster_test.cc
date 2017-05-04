#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "common/network/utility.h"
#include "common/upstream/logical_dns_cluster.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;

namespace Upstream {

class LogicalDnsClusterTest : public testing::Test {
public:
  void setup(const std::string& json) {
    Json::ObjectPtr config = Json::Factory::loadFromString(json);
    resolve_timer_ = new Event::MockTimer(&dispatcher_);
    cluster_.reset(new LogicalDnsCluster(*config, runtime_, stats_store_, ssl_context_manager_,
                                         dns_resolver_, tls_, dispatcher_));
    cluster_->addMemberUpdateCb(
        [&](const std::vector<HostSharedPtr>&, const std::vector<HostSharedPtr>&)
            -> void { membership_updated_.ready(); });
    cluster_->setInitializedCb([&]() -> void { initialized_.ready(); });
  }

  void expectResolve() {
    EXPECT_CALL(dns_resolver_, resolve("foo.bar.com", _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsResolver::ResolveCb cb)
                             -> Network::ActiveDnsQuery* {
                               dns_callback_ = cb;
                               return &active_dns_query_;
                             }));
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<Network::MockDnsResolver> dns_resolver_;
  Network::MockActiveDnsQuery active_dns_query_;
  Network::DnsResolver::ResolveCb dns_callback_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  std::unique_ptr<LogicalDnsCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
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

// Validate that if the DNS resolves immediately, during the LogicalDnsCluster
// constructor, we have the expected host state and initialization callback
// invocation.
TEST_F(LogicalDnsClusterTest, ImmediateResolve) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}]
  }
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(dns_resolver_, resolve("foo.bar.com", _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsResolver::ResolveCb cb)
                           -> Network::ActiveDnsQuery* {
                             EXPECT_CALL(*resolve_timer_, enableTimer(_));
                             cb(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
                             return nullptr;
                           }));
  setup(json);
  EXPECT_EQ(1UL, cluster_->hosts().size());
  EXPECT_EQ(1UL, cluster_->healthyHosts().size());
  EXPECT_EQ("foo.bar.com", cluster_->hosts()[0]->hostname());
  tls_.shutdownThread();
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
  dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  EXPECT_EQ(1UL, cluster_->hosts().size());
  EXPECT_EQ(1UL, cluster_->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->hostsPerZone().size());
  EXPECT_EQ(0UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(cluster_->hosts()[0], cluster_->healthyHosts()[0]);
  HostSharedPtr logical_host = cluster_->hosts()[0];

  EXPECT_CALL(dispatcher_, createClientConnection_(
                               PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443"))))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  logical_host->createConnection(dispatcher_);
  logical_host->outlierDetector().putHttpResponseCode(200);

  expectResolve();
  resolve_timer_->callback_();

  // Should not cause any changes.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2", "127.0.0.3"}));

  EXPECT_EQ(logical_host, cluster_->hosts()[0]);
  EXPECT_CALL(dispatcher_, createClientConnection_(
                               PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443"))))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  Host::CreateConnectionData data = logical_host->createConnection(dispatcher_);
  EXPECT_FALSE(data.host_description_->canary());
  EXPECT_EQ(&cluster_->hosts()[0]->cluster(), &data.host_description_->cluster());
  EXPECT_EQ(&cluster_->hosts()[0]->stats(), &data.host_description_->stats());
  EXPECT_EQ("127.0.0.1:443", data.host_description_->address()->asString());
  EXPECT_EQ("", data.host_description_->zone());
  EXPECT_EQ("foo.bar.com", data.host_description_->hostname());
  data.host_description_->outlierDetector().putHttpResponseCode(200);

  expectResolve();
  resolve_timer_->callback_();

  // Should cause a change.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3", "127.0.0.1", "127.0.0.2"}));

  EXPECT_EQ(logical_host, cluster_->hosts()[0]);
  EXPECT_CALL(dispatcher_, createClientConnection_(
                               PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443"))))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  logical_host->createConnection(dispatcher_);

  expectResolve();
  resolve_timer_->callback_();

  // Empty should not cause any change.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_({});

  EXPECT_EQ(logical_host, cluster_->hosts()[0]);
  EXPECT_CALL(dispatcher_, createClientConnection_(
                               PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443"))))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  logical_host->createConnection(dispatcher_);

  // Make sure we cancel.
  EXPECT_CALL(active_dns_query_, cancel());
  expectResolve();
  resolve_timer_->callback_();

  tls_.shutdownThread();
}

} // Upstream
