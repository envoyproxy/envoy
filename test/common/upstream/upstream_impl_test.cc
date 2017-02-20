#include "envoy/api/api.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::ContainerEq;
using testing::Invoke;
using testing::NiceMock;

namespace Upstream {

static std::list<std::string> hostListToAddresses(const std::vector<HostPtr>& hosts) {
  std::list<std::string> addresses;
  for (const HostPtr& host : hosts) {
    addresses.push_back(host->address()->asString());
  }

  return addresses;
}

struct ResolverData {
  ResolverData(Network::MockDnsResolver& dns_resolver, Event::MockDispatcher& dispatcher) {
    timer_ = new Event::MockTimer(&dispatcher);
    expectResolve(dns_resolver);
  }

  void expectResolve(Network::MockDnsResolver& dns_resolver) {
    EXPECT_CALL(dns_resolver, resolve(_, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsResolver::ResolveCb cb)
                             -> Network::ActiveDnsQuery* {
                               dns_callback_ = cb;
                               return &active_dns_query_;
                             }))
        .RetiresOnSaturation();
  }

  Event::MockTimer* timer_;
  Network::DnsResolver::ResolveCb dns_callback_;
  Network::MockActiveDnsQuery active_dns_query_;
};

TEST(StrictDnsClusterImplTest, ImmediateResolve) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Network::MockDnsResolver> dns_resolver;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  ReadyWatcher initialized;

  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "strict_dns",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}]
  }
  )EOF";

  EXPECT_CALL(initialized, ready());
  EXPECT_CALL(dns_resolver, resolve("foo.bar.com", _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsResolver::ResolveCb cb)
                           -> Network::ActiveDnsQuery* {
                             cb(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
                             return nullptr;
                           }));
  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  StrictDnsClusterImpl cluster(*loader, runtime, stats, ssl_context_manager, dns_resolver,
                               dispatcher);
  cluster.setInitializedCb([&]() -> void { initialized.ready(); });
  EXPECT_EQ(2UL, cluster.hosts().size());
  EXPECT_EQ(2UL, cluster.healthyHosts().size());
}

TEST(StrictDnsClusterImplTest, Basic) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Network::MockDnsResolver> dns_resolver;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;

  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver2(dns_resolver, dispatcher);
  ResolverData resolver1(dns_resolver, dispatcher);

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

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  StrictDnsClusterImpl cluster(*loader, runtime, stats, ssl_context_manager, dns_resolver,
                               dispatcher);
  EXPECT_EQ(43U, cluster.info()->resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_EQ(57U,
            cluster.info()->resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_EQ(50U, cluster.info()->resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_EQ(10U, cluster.info()->resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_EQ(1U, cluster.info()->resourceManager(ResourcePriority::High).connections().max());
  EXPECT_EQ(2U, cluster.info()->resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::High).requests().max());
  EXPECT_EQ(4U, cluster.info()->resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(3U, cluster.info()->maxRequestsPerConnection());
  EXPECT_EQ(Http::CodecOptions::NoCompression, cluster.info()->httpCodecOptions());

  cluster.info()->stats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats.counter("cluster.name.upstream_rq_total").value());

  EXPECT_CALL(runtime.snapshot_, featureEnabled("upstream.maintenance_mode.name", 0));
  EXPECT_FALSE(cluster.info()->maintenanceMode());

  ReadyWatcher membership_updated;
  cluster.addMemberUpdateCb([&](const std::vector<HostPtr>&, const std::vector<HostPtr>&)
                                -> void { membership_updated.ready(); });

  resolver1.expectResolve(dns_resolver);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
  EXPECT_THAT(std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
              ContainerEq(hostListToAddresses(cluster.hosts())));

  resolver1.expectResolve(dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
              ContainerEq(hostListToAddresses(cluster.hosts())));

  resolver1.expectResolve(dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
              ContainerEq(hostListToAddresses(cluster.hosts())));

  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3"}));
  EXPECT_THAT(std::list<std::string>({"127.0.0.3:11001"}),
              ContainerEq(hostListToAddresses(cluster.hosts())));

  // Make sure we de-dup the same address.
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));
  EXPECT_THAT(std::list<std::string>({"127.0.0.3:11001", "10.0.0.1:11002"}),
              ContainerEq(hostListToAddresses(cluster.hosts())));

  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(0UL, cluster.hostsPerZone().size());
  EXPECT_EQ(0UL, cluster.healthyHostsPerZone().size());

  for (const HostPtr& host : cluster.hosts()) {
    EXPECT_EQ(cluster.info().get(), &host->cluster());
  }

  // Make sure we cancel.
  resolver1.expectResolve(dns_resolver);
  resolver1.timer_->callback_();
  resolver2.expectResolve(dns_resolver);
  resolver2.timer_->callback_();

  EXPECT_CALL(resolver1.active_dns_query_, cancel());
  EXPECT_CALL(resolver2.active_dns_query_, cancel());
}

#if 0
TEST(HostImplTest, HostCluster) {
  MockCluster cluster;
  HostImpl host(cluster.info, "", Network::Utility::resolveUrl("tcp://10.0.0.1:1234"), false, 1,
                "");
  EXPECT_EQ(cluster.info_.get(), &host.cluster());
  EXPECT_EQ("", host.hostname());
  EXPECT_FALSE(host.canary());
  EXPECT_EQ("", host.zone());
}

TEST(HostImplTest, Weight) {
  MockCluster cluster;

  {
    HostImpl host(cluster.info, "", Network::Utility::resolveUrl("tcp://10.0.0.1:1234"), false, 0,
                  "");
    EXPECT_EQ(1U, host.weight());
  }

  {
    HostImpl host(cluster.info, "", Network::Utility::resolveUrl("tcp://10.0.0.1:1234"), false, 101,
                  "");
    EXPECT_EQ(100U, host.weight());
  }

  {
    HostImpl host(cluster.info, "", Network::Utility::resolveUrl("tcp://10.0.0.1:1234"), false, 50,
                  "");
    EXPECT_EQ(50U, host.weight());
    host.weight(51);
    EXPECT_EQ(51U, host.weight());
    host.weight(0);
    EXPECT_EQ(1U, host.weight());
    host.weight(101);
    EXPECT_EQ(100U, host.weight());
  }
}

TEST(HostImplTest, HostameCanaryAndZone) {
  MockCluster cluster;
  HostImpl host(cluster.info, "lyft.com", Network::Utility::resolveUrl("tcp://10.0.0.1:1234"), true,
                1, "hello");
  EXPECT_EQ(cluster.info_.get(), &host.cluster());
  EXPECT_EQ("lyft.com", host.hostname());
  EXPECT_TRUE(host.canary());
  EXPECT_EQ("hello", host.zone());
}
#endif

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

  Json::ObjectPtr config = Json::Factory::LoadFromString(json);
  StaticClusterImpl cluster(*config, runtime, stats, ssl_context_manager);

  Outlier::MockDetector* detector = new Outlier::MockDetector();
  EXPECT_CALL(*detector, addChangedStateCb(_));
  cluster.setOutlierDetector(Outlier::DetectorPtr{detector});

  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());

  // Set a single host as having failed and fire outlier detector callbacks. This should result
  // in only a single healthy host.
  cluster.hosts()[0]->outlierDetector().putHttpResponseCode(503);
  cluster.hosts()[0]->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.hosts()[0]);
  EXPECT_EQ(1UL, cluster.healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_NE(cluster.healthyHosts()[0], cluster.hosts()[0]);

  // Bring the host back online.
  cluster.hosts()[0]->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.hosts()[0]);
  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());
}

TEST(StaticClusterImplTest, HealthyStat) {
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

  Json::ObjectPtr config = Json::Factory::LoadFromString(json);
  StaticClusterImpl cluster(*config, runtime, stats, ssl_context_manager);

  Outlier::MockDetector* outlier_detector = new NiceMock<Outlier::MockDetector>();
  cluster.setOutlierDetector(Outlier::DetectorPtr{outlier_detector});

  MockHealthChecker* health_checker = new NiceMock<MockHealthChecker>();
  cluster.setHealthChecker(HealthCheckerPtr{health_checker});

  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());

  cluster.hosts()[0]->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.hosts()[0]);
  EXPECT_EQ(1UL, cluster.healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.hosts()[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.hosts()[0], true);
  EXPECT_EQ(1UL, cluster.healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.hosts()[0]->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.hosts()[0]);
  EXPECT_EQ(1UL, cluster.healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.hosts()[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.hosts()[0], true);
  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());

  cluster.hosts()[0]->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.hosts()[0]);
  EXPECT_EQ(1UL, cluster.healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.hosts()[1]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.hosts()[1], true);
  EXPECT_EQ(0UL, cluster.healthyHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());
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

  Json::ObjectPtr config = Json::Factory::LoadFromString(json);
  StaticClusterImpl cluster(*config, runtime, stats, ssl_context_manager);
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_EQ(1024U,
            cluster.info()->resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::High).connections().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::High).requests().max());
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(0U, cluster.info()->maxRequestsPerConnection());
  EXPECT_EQ(0U, cluster.info()->httpCodecOptions());
  EXPECT_EQ(LoadBalancerType::Random, cluster.info()->lbType());
  EXPECT_THAT(std::list<std::string>({"10.0.0.1:11001", "10.0.0.2:11002"}),
              ContainerEq(hostListToAddresses(cluster.hosts())));
  EXPECT_EQ(2UL, cluster.healthyHosts().size());
  EXPECT_EQ(0UL, cluster.hostsPerZone().size());
  EXPECT_EQ(0UL, cluster.healthyHostsPerZone().size());
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

  Json::ObjectPtr config = Json::Factory::LoadFromString(json);
  EXPECT_THROW(StaticClusterImpl(*config, runtime, stats, ssl_context_manager), EnvoyException);
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

  Json::ObjectPtr config = Json::Factory::LoadFromString(json);
  EXPECT_THROW(StaticClusterImpl(*config, runtime, stats, ssl_context_manager), EnvoyException);
}

TEST(ClusterDefinitionTest, BadClusterConfig) {
  std::string json = R"EOF(
  {
    "name": "cluster_1",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "fake_type" : "expected_failure",
    "hosts": [{"url": "tcp://127.0.0.1:11001"}]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  EXPECT_THROW(loader->validateSchema(Json::Schema::CLUSTER_SCHEMA), Json::Exception);
}

} // Upstream
