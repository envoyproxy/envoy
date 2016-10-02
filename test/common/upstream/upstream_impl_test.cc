#include "envoy/api/api.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/json/json_loader.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::ContainerEq;
using testing::Invoke;
using testing::NiceMock;

namespace Upstream {

static std::list<std::string> hostListToURLs(const std::vector<HostPtr>& hosts) {
  std::list<std::string> urls;
  for (const HostPtr& host : hosts) {
    urls.push_back(host->url());
  }

  return urls;
}

struct ResolverData {
  ResolverData(Network::MockDnsResolver& dns_resolver) {
    timer_ = new Event::MockTimer(&dns_resolver.dispatcher_);
    expectResolve(dns_resolver);
  }

  void expectResolve(Network::MockDnsResolver& dns_resolver) {
    EXPECT_CALL(dns_resolver, resolve(_, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsResolver::ResolveCb cb)
                             -> void { dns_callback_ = cb; }))
        .RetiresOnSaturation();
  }

  Event::MockTimer* timer_;
  Network::DnsResolver::ResolveCb dns_callback_;
};

TEST(StrictDnsClusterImplTest, Basic) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Network::MockDnsResolver> dns_resolver;
  NiceMock<Runtime::MockLoader> runtime;

  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver2(dns_resolver);
  ResolverData resolver1(dns_resolver);

  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "strict_dns",
    "dns_refresh_rate_ms": 4000,
    "lb_type": "round_robin",
    "circuit_breakers": {
      "default": {
        "max_connections": 43,
        "max_pending_requests": 57,
        "max_requests": 50,
        "max_retries": 10
      },
      "high": {
        "max_connections": 1,
        "max_pending_requests": 2,
        "max_requests": 3,
        "max_retries": 4
      }
    },
    "max_requests_per_connection": 3,
    "http_codec_options": "no_compression",
    "hosts": [{"url": "tcp://localhost:11001"},
              {"url": "tcp://localhost2:11002"}]
  }
  )EOF";

  Json::StringLoader loader(json);
  StrictDnsClusterImpl cluster(loader, runtime, stats, ssl_context_manager, dns_resolver);
  EXPECT_EQ(43U, cluster.resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_EQ(57U, cluster.resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_EQ(50U, cluster.resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_EQ(10U, cluster.resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_EQ(1U, cluster.resourceManager(ResourcePriority::High).connections().max());
  EXPECT_EQ(2U, cluster.resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_EQ(3U, cluster.resourceManager(ResourcePriority::High).requests().max());
  EXPECT_EQ(4U, cluster.resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(3U, cluster.maxRequestsPerConnection());
  EXPECT_EQ(Http::CodecOptions::NoCompression, cluster.httpCodecOptions());
  ReadyWatcher membership_updated;
  cluster.addMemberUpdateCb([&](const std::vector<HostPtr>&, const std::vector<HostPtr>&)
                                -> void { membership_updated.ready(); });

  resolver1.expectResolve(dns_resolver);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_({"127.0.0.1", "127.0.0.2"});
  EXPECT_THAT(std::list<std::string>({"tcp://127.0.0.1:11001", "tcp://127.0.0.2:11001"}),
              ContainerEq(hostListToURLs(cluster.hosts())));

  resolver1.expectResolve(dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_({"127.0.0.2", "127.0.0.1"});
  EXPECT_THAT(std::list<std::string>({"tcp://127.0.0.1:11001", "tcp://127.0.0.2:11001"}),
              ContainerEq(hostListToURLs(cluster.hosts())));

  resolver1.expectResolve(dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_({"127.0.0.2", "127.0.0.1"});
  EXPECT_THAT(std::list<std::string>({"tcp://127.0.0.1:11001", "tcp://127.0.0.2:11001"}),
              ContainerEq(hostListToURLs(cluster.hosts())));

  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_({"127.0.0.3"});
  EXPECT_THAT(std::list<std::string>({"tcp://127.0.0.3:11001"}),
              ContainerEq(hostListToURLs(cluster.hosts())));

  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_({"10.0.0.1"});
  EXPECT_THAT(std::list<std::string>({"tcp://127.0.0.3:11001", "tcp://10.0.0.1:11002"}),
              ContainerEq(hostListToURLs(cluster.hosts())));

  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(0UL, cluster.localZoneHosts().size());
  EXPECT_EQ(0UL, cluster.localZoneHealthyHosts().size());

  for (const HostPtr& host : cluster.hosts()) {
    EXPECT_EQ(&cluster, &host->cluster());
  }
}

TEST(HostImplTest, HostCluster) {
  MockCluster cluster;
  HostImpl host(cluster, "tcp://10.0.0.1:1234", false, 1, "");
  EXPECT_EQ(&cluster, &host.cluster());
  EXPECT_FALSE(host.canary());
  EXPECT_EQ("", host.zone());
}

TEST(HostImplTest, Weight) {
  MockCluster cluster;

  {
    HostImpl host(cluster, "tcp://10.0.0.1:1234", false, 0, "");
    EXPECT_EQ(1U, host.weight());
  }

  {
    HostImpl host(cluster, "tcp://10.0.0.1:1234", false, 101, "");
    EXPECT_EQ(100U, host.weight());
  }

  {
    HostImpl host(cluster, "tcp://10.0.0.1:1234", false, 50, "");
    EXPECT_EQ(50U, host.weight());
    host.weight(51);
    EXPECT_EQ(51U, host.weight());
    host.weight(0);
    EXPECT_EQ(1U, host.weight());
    host.weight(101);
    EXPECT_EQ(100U, host.weight());
  }
}

TEST(HostImplTest, CanaryAndZone) {
  MockCluster cluster;
  HostImpl host(cluster, "tcp://10.0.0.1:1234", true, 1, "hello");
  EXPECT_EQ(&cluster, &host.cluster());
  EXPECT_TRUE(host.canary());
  EXPECT_EQ("hello", host.zone());
}

TEST(HostImplTest, MalformedUrl) {
  MockCluster cluster;
  EXPECT_THROW(HostImpl(cluster, "fake\\10.0.0.1:1234", false, 1, ""), EnvoyException);
}

TEST(StaticClusterImplTest, OutlierDetector) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "random",
    "hosts": [{"url": "tcp://10.0.0.1:11001"},
              {"url": "tcp://10.0.0.2:11002"}]
  }
  )EOF";

  Json::StringLoader config(json);
  StaticClusterImpl cluster(config, runtime, stats, ssl_context_manager);

  MockOutlierDetector* detector = new MockOutlierDetector();
  EXPECT_CALL(*detector, addChangedStateCb(_));
  cluster.setOutlierDetector(OutlierDetectorPtr{detector});

  EXPECT_EQ(2UL, cluster.healthyHosts().size());

  // Set a single host as having failed and fire outlier detector callbacks. This should result
  // in only a single healthy host.
  cluster.hosts()[0]->outlierDetector().putHttpResponseCode(503);
  cluster.hosts()[0]->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.hosts()[0]);
  EXPECT_EQ(1UL, cluster.healthyHosts().size());
  EXPECT_NE(cluster.healthyHosts()[0], cluster.hosts()[0]);

  // Bring the host back online.
  cluster.hosts()[0]->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.hosts()[0]);
  EXPECT_EQ(2UL, cluster.healthyHosts().size());
}

TEST(StaticClusterImplTest, UrlConfig) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "random",
    "hosts": [{"url": "tcp://10.0.0.1:11001"},
              {"url": "tcp://10.0.0.2:11002"}]
  }
  )EOF";

  Json::StringLoader config(json);
  StaticClusterImpl cluster(config, runtime, stats, ssl_context_manager);
  EXPECT_EQ(1024U, cluster.resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_EQ(1024U, cluster.resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_EQ(1024U, cluster.resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_EQ(3U, cluster.resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_EQ(1024U, cluster.resourceManager(ResourcePriority::High).connections().max());
  EXPECT_EQ(1024U, cluster.resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_EQ(1024U, cluster.resourceManager(ResourcePriority::High).requests().max());
  EXPECT_EQ(3U, cluster.resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(0U, cluster.maxRequestsPerConnection());
  EXPECT_EQ(0U, cluster.httpCodecOptions());
  EXPECT_EQ(LoadBalancerType::Random, cluster.lbType());
  EXPECT_THAT(std::list<std::string>({"tcp://10.0.0.1:11001", "tcp://10.0.0.2:11002"}),
              ContainerEq(hostListToURLs(cluster.hosts())));
  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(0UL, cluster.localZoneHosts().size());
  EXPECT_EQ(0UL, cluster.localZoneHealthyHosts().size());
}

TEST(StaticClusterImplTest, UnsupportedLBType) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "fakelbtype",
    "hosts": [{"url": "tcp://192.168.1.1:22"},
              {"url": "tcp://192.168.1.2:44"}]
  }
  )EOF";

  Json::StringLoader config(json);
  EXPECT_THROW(StaticClusterImpl(config, runtime, stats, ssl_context_manager), EnvoyException);
}

TEST(StaticClusterImplTest, UnsupportedFeature) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "features": "fake",
    "hosts": [{"url": "tcp://192.168.1.1:22"},
              {"url": "tcp://192.168.1.2:44"}]
  }
  )EOF";

  Json::StringLoader config(json);
  EXPECT_THROW(StaticClusterImpl(config, runtime, stats, ssl_context_manager), EnvoyException);
}

} // Upstream
