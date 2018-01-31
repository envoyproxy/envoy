#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "common/network/utility.h"
#include "common/upstream/logical_dns_cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Upstream {

class LogicalDnsClusterTest : public testing::Test {
public:
  void setup(const std::string& json) {
    resolve_timer_ = new Event::MockTimer(&dispatcher_);
    NiceMock<MockClusterManager> cm;
    cluster_.reset(new LogicalDnsCluster(parseClusterFromJson(json), runtime_, stats_store_,
                                         ssl_context_manager_, dns_resolver_, tls_, cm, dispatcher_,
                                         false));
    cluster_->prioritySet().addMemberUpdateCb(
        [&](uint32_t, const std::vector<HostSharedPtr>&,
            const std::vector<HostSharedPtr>&) -> void { membership_updated_.ready(); });
    cluster_->initialize([&]() -> void { initialized_.ready(); });
  }

  void expectResolve(Network::DnsLookupFamily dns_lookup_family) {
    EXPECT_CALL(*dns_resolver_, resolve("foo.bar.com", dns_lookup_family, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                             Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          dns_callback_ = cb;
          return &active_dns_query_;
        }));
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  Network::MockActiveDnsQuery active_dns_query_;
  Network::DnsResolver::ResolveCb dns_callback_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  std::shared_ptr<LogicalDnsCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
};

typedef std::tuple<std::string, Network::DnsLookupFamily, std::list<std::string>>
    LogicalDnsConfigTuple;
std::vector<LogicalDnsConfigTuple> generateLogicalDnsParams() {
  std::vector<LogicalDnsConfigTuple> dns_config;
  {
    std::string family_json("");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "v4_only",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "v6_only",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V6Only);
    std::list<std::string> dns_response{"::1", "::2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "auto",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"::1"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  return dns_config;
}

class LogicalDnsParamTest : public LogicalDnsClusterTest,
                            public testing::WithParamInterface<LogicalDnsConfigTuple> {};

INSTANTIATE_TEST_CASE_P(DnsParam, LogicalDnsParamTest,
                        testing::ValuesIn(generateLogicalDnsParams()));

// Validate that if the DNS resolves immediately, during the LogicalDnsCluster
// constructor, we have the expected host state and initialization callback
// invocation.
TEST_P(LogicalDnsParamTest, ImmediateResolve) {
  const std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
  )EOF" + std::get<0>(GetParam()) +
                           R"EOF(
    "hosts": [{"url": "tcp://foo.bar.com:443"}]
  }
  )EOF";

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*dns_resolver_, resolve("foo.bar.com", std::get<1>(GetParam()), _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        EXPECT_CALL(*resolve_timer_, enableTimer(_));
        cb(TestUtility::makeDnsResponse(std::get<2>(GetParam())));
        return nullptr;
      }));
  setup(json);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("foo.bar.com",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthChecker().setUnhealthy();
  tls_.shutdownThread();
}

TEST_F(LogicalDnsClusterTest, BadConfig) {
  const std::string json = R"EOF(
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
  const std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}],
    "dns_refresh_rate_ms": 4000
  }
  )EOF";

  expectResolve(Network::DnsLookupFamily::V4Only);
  setup(json);

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000)));
  dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().size());
  EXPECT_EQ(0UL,
            cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().size());
  EXPECT_EQ(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
            cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts()[0]);
  HostSharedPtr logical_host = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0];

  EXPECT_CALL(dispatcher_,
              createClientConnection_(
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  logical_host->createConnection(dispatcher_);
  logical_host->outlierDetector().putHttpResponseCode(200);

  expectResolve(Network::DnsLookupFamily::V4Only);
  resolve_timer_->callback_();

  // Should not cause any changes.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2", "127.0.0.3"}));

  EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_CALL(dispatcher_,
              createClientConnection_(
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  Host::CreateConnectionData data = logical_host->createConnection(dispatcher_);
  EXPECT_FALSE(data.host_description_->canary());
  EXPECT_EQ(&cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->cluster(),
            &data.host_description_->cluster());
  EXPECT_EQ(&cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->stats(),
            &data.host_description_->stats());
  EXPECT_EQ("127.0.0.1:443", data.host_description_->address()->asString());
  EXPECT_EQ("", data.host_description_->locality().region());
  EXPECT_EQ("", data.host_description_->locality().zone());
  EXPECT_EQ("", data.host_description_->locality().sub_zone());
  EXPECT_EQ("foo.bar.com", data.host_description_->hostname());
  EXPECT_EQ(&envoy::api::v2::Metadata::default_instance(), &data.host_description_->metadata());
  data.host_description_->outlierDetector().putHttpResponseCode(200);
  data.host_description_->healthChecker().setUnhealthy();

  expectResolve(Network::DnsLookupFamily::V4Only);
  resolve_timer_->callback_();

  // Should cause a change.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3", "127.0.0.1", "127.0.0.2"}));

  EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_CALL(dispatcher_,
              createClientConnection_(
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  logical_host->createConnection(dispatcher_);

  expectResolve(Network::DnsLookupFamily::V4Only);
  resolve_timer_->callback_();

  // Empty should not cause any change.
  EXPECT_CALL(*resolve_timer_, enableTimer(_));
  dns_callback_({});

  EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_CALL(dispatcher_,
              createClientConnection_(
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  logical_host->createConnection(dispatcher_);

  // Make sure we cancel.
  EXPECT_CALL(active_dns_query_, cancel());
  expectResolve(Network::DnsLookupFamily::V4Only);
  resolve_timer_->callback_();

  tls_.shutdownThread();
}

} // namespace Upstream
} // namespace Envoy
