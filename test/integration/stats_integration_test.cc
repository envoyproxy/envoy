#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/config/well_known_names.h"
#include "source/common/memory/stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/config/utility.h"
#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class StatsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                             public BaseIntegrationTest {
public:
  StatsIntegrationTest() : BaseIntegrationTest(GetParam()) {}

  void initialize() override { BaseIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsIntegrationTest, WithDefaultConfig) {
  initialize();

  auto live = test_server_->gauge("server.live");
  EXPECT_EQ(live->value(), 1);
  EXPECT_EQ(live->tags().size(), 0);

  auto counter = test_server_->counter("http.config_test.rq_total");
  EXPECT_EQ(counter->tags().size(), 1);
  EXPECT_EQ(counter->tags()[0].name_, "envoy.http_conn_manager_prefix");
  EXPECT_EQ(counter->tags()[0].value_, "config_test");
}

TEST_P(StatsIntegrationTest, WithoutDefaultTagExtractors) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
  });
  initialize();

  auto counter = test_server_->counter("http.config_test.rq_total");
  EXPECT_EQ(counter->tags().size(), 0);
}

TEST_P(StatsIntegrationTest, WithDefaultTagExtractors) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(true);
  });
  initialize();

  auto counter = test_server_->counter("http.config_test.rq_total");
  EXPECT_EQ(counter->tags().size(), 1);
  EXPECT_EQ(counter->tags()[0].name_, "envoy.http_conn_manager_prefix");
  EXPECT_EQ(counter->tags()[0].value_, "config_test");
}

// Given: a. use_all_default_tags = false, b. a tag specifier has the same name
// as a default tag extractor name but also has use defined regex.
// In this case we don't use default tag extractors (since use_all_default_tags
// is set to false explicitly) and just treat the tag specifier as a normal tag
// specifier having use defined regex.
TEST_P(StatsIntegrationTest, WithDefaultTagExtractorNameWithUserDefinedRegex) {
  std::string tag_name = Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
    auto tag_specifier = bootstrap.mutable_stats_config()->mutable_stats_tags()->Add();
    tag_specifier->set_tag_name(tag_name);
    tag_specifier->set_regex("((.*))");
  });
  initialize();

  auto counter = test_server_->counter("http.config_test.rq_total");
  EXPECT_EQ(counter->tags().size(), 1);
  EXPECT_EQ(counter->tags()[0].name_, tag_name);
  EXPECT_EQ(counter->tags()[0].value_, "http.config_test.rq_total");
}

TEST_P(StatsIntegrationTest, WithTagSpecifierMissingTagValue) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
    auto tag_specifier = bootstrap.mutable_stats_config()->mutable_stats_tags()->Add();
    tag_specifier->set_tag_name("envoy.http_conn_manager_prefix");
  });
  initialize();

  auto counter = test_server_->counter("http.config_test.rq_total");
  EXPECT_EQ(counter->tags().size(), 1);
  EXPECT_EQ(counter->tags()[0].name_, "envoy.http_conn_manager_prefix");
  EXPECT_EQ(counter->tags()[0].value_, "config_test");
}

TEST_P(StatsIntegrationTest, WithTagSpecifierWithRegex) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
    auto tag_specifier = bootstrap.mutable_stats_config()->mutable_stats_tags()->Add();
    tag_specifier->set_tag_name("my.http_conn_manager_prefix");
    tag_specifier->set_regex(R"(^(?:|listener(?=\.).*?\.)http\.((.*?)\.))");
  });
  initialize();

  auto counter = test_server_->counter("http.config_test.rq_total");
  EXPECT_EQ(counter->tags().size(), 1);
  EXPECT_EQ(counter->tags()[0].name_, "my.http_conn_manager_prefix");
  EXPECT_EQ(counter->tags()[0].value_, "config_test");
}

TEST_P(StatsIntegrationTest, WithTagSpecifierWithFixedValue) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto tag_specifier = bootstrap.mutable_stats_config()->mutable_stats_tags()->Add();
    tag_specifier->set_tag_name("test.x");
    tag_specifier->set_fixed_value("xxx");
  });
  initialize();

  auto live = test_server_->gauge("server.live");
  EXPECT_EQ(live->value(), 1);
  EXPECT_EQ(live->tags().size(), 1);
  EXPECT_EQ(live->tags()[0].name_, "test.x");
  EXPECT_EQ(live->tags()[0].value_, "xxx");
}

// TODO(cmluciano) Refactor once https://github.com/envoyproxy/envoy/issues/5624 is solved
// TODO(cmluciano) Add options to measure multiple workers & without stats
// This class itself does not add additional tests. It is a helper for use in other tests measuring
// cluster overhead.
class ClusterMemoryTestHelper : public BaseIntegrationTest {
public:
  ClusterMemoryTestHelper()
      : BaseIntegrationTest(testing::TestWithParam<Network::Address::IpVersion>::GetParam()) {
    use_real_stats_ = true;
  }

  static size_t computeMemoryDelta(int initial_num_clusters, int initial_num_hosts,
                                   int final_num_clusters, int final_num_hosts, bool allow_stats) {
    // Use the same number of fake upstreams for both helpers in order to exclude memory overhead
    // added by the fake upstreams.
    int fake_upstreams_count = 1 + final_num_clusters * final_num_hosts;

    size_t initial_memory;
    {
      ClusterMemoryTestHelper helper;
      helper.setUpstreamCount(fake_upstreams_count);
      helper.skipPortUsageValidation();
      initial_memory =
          helper.clusterMemoryHelper(initial_num_clusters, initial_num_hosts, allow_stats);
    }

    ClusterMemoryTestHelper helper;
    helper.setUpstreamCount(fake_upstreams_count);
    return helper.clusterMemoryHelper(final_num_clusters, final_num_hosts, allow_stats) -
           initial_memory;
  }

private:
  /**
   * @param num_clusters number of clusters appended to bootstrap_config
   * @param allow_stats if false, enable set_reject_all in stats_config
   * @return size_t the total memory allocated
   */
  size_t clusterMemoryHelper(int num_clusters, int num_hosts, bool allow_stats) {
    Stats::TestUtil::MemoryTest memory_test;
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      if (!allow_stats) {
        bootstrap.mutable_stats_config()->mutable_stats_matcher()->set_reject_all(true);
      }
      for (int i = 1; i < num_clusters; ++i) {
        auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
        cluster->set_name(absl::StrCat("cluster_", i));
      }

      for (int i = 0; i < num_clusters; ++i) {
        auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(i);
        for (int j = 0; j < num_hosts; ++j) {
          auto* host = cluster->mutable_load_assignment()
                           ->mutable_endpoints(0)
                           ->add_lb_endpoints()
                           ->mutable_endpoint()
                           ->mutable_address();
          auto* socket_address = host->mutable_socket_address();
          socket_address->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
          socket_address->set_address("0.0.0.0");
        }
      }
    });
    initialize();

    return memory_test.consumedBytes();
  }
};

class ClusterMemoryTestRunner : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ClusterMemoryTestRunner()
      : ip_version_(testing::TestWithParam<Network::Address::IpVersion>::GetParam()) {}

  Network::Address::IpVersion ip_version_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterMemoryTestRunner,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClusterMemoryTestRunner, MemoryLargeClusterSize) {
  // A unique instance of ClusterMemoryTest allows for multiple runs of Envoy with
  // differing configuration. This is necessary for measuring the memory consumption
  // between the different instances within the same test.
  const size_t m100 = ClusterMemoryTestHelper::computeMemoryDelta(1, 0, 101, 0, true);
  const size_t m_per_cluster = (m100) / 100;

  // Note: if you are increasing this golden value because you are adding a
  // stat, please confirm that this will be generally useful to most Envoy
  // users. Otherwise you are adding to the per-cluster memory overhead, which
  // will be significant for Envoy installations that are massively
  // multi-tenant.
  //
  // History of golden values:
  //
  // Date        PR       Bytes Per Cluster   Notes
  //                      exact upper-bound
  // ----------  -----    -----------------   -----
  // 2019/08/09  7882     35489       36000   Initial version
  // 2019/09/02  8118     34585       34500   Share symbol-tables in cluster/host stats.
  // 2019/09/16  8100     34585       34500   Add transport socket matcher in cluster.
  // 2019/09/25  8226     34777       35000   dns: enable dns failure refresh rate configuration
  // 2019/09/30  8354     34969       35000   Implement transport socket match.
  // 2019/10/17  8537     34966       35000   add new enum value HTTP3
  // 2019/10/17  8484     34998       35000   stats: add unit support to histogram
  // 2019/11/01  8859     35221       36000   build: switch to libc++ by default
  // 2019/11/15  9040     35029       35500   build: update protobuf to 3.10.1
  // 2019/11/15  9031     35061       35500   upstream: track whether cluster is local
  // 2019/12/10  8779     35053       35000   use var-length coding for name lengths
  // 2020/01/07  9069     35548       35700   upstream: Implement retry concurrency budgets
  // 2020/01/07  9564     35580       36000   RefcountPtr for CentralCache.
  // 2020/01/09  8889     35644       36000   api: add UpstreamHttpProtocolOptions message
  // 2019/01/09  9227     35772       36500   router: per-cluster histograms w/ timeout budget
  // 2020/01/12  9633     35932       36500   config: support recovery of original message when
  //                                          upgrading.
  // 2020/03/16  9964     36220       36800   http2: support custom SETTINGS parameters.
  // 2020/03/24  10501    36300       36800   upstream: upstream_rq_retry_limit_exceeded.
  // 2020/04/02  10624    35564       36000   Use 100 clusters rather than 1000 to avoid timeouts
  // 2020/04/07  10661    35557       36000   fix clang tidy on master
  // 2020/04/23  10531    36281       36800   http: max stream duration upstream support.
  // 2020/04/23  10661    36537       37000   per-listener connection limits
  // 2020/05/05  10908    36345       36800   router: add InternalRedirectPolicy and predicate
  // 2020/05/13  10531    36537       36800   Refactor resource manager
  // 2020/05/20  11223    36603       36800   Add primary clusters tracking to cluster manager.
  // 2020/06/10  11561    36603       36923   Make upstreams pluggable
  // 2020/06/29  11751    36827       38000   Improve time complexity of removing callback handle.
  // 2020/07/07  11252    37083       38000   Introduce Least Request LB active request bias config
  // 2020/07/15  11748    37115       38000   Stream error on invalid messaging
  // 2020/07/20  11559    36859       38000   stats: add histograms for request/response headers
  //                                          and body sizes.
  // 2020/07/21  12034    36923       38000   Add configurable histogram buckets.
  // 2020/07/31  12035    37114       38000   Init manager store unready targets in hash map.
  // 2020/08/10  12275    37061       38000   Re-organize tls histogram maps to improve continuity.
  // 2020/08/11  12202    37061       38500   router: add new retry back-off strategy
  // 2020/09/11  12973                38993   upstream: predictive preconnect
  // 2020/10/02  13251                39326   switch to google tcmalloc

  // Note: when adjusting this value: EXPECT_MEMORY_EQ is active only in CI
  // 'release' builds, where we control the platform and tool-chain. So you
  // will need to find the correct value only after failing CI and looking
  // at the logs.
  //
  // On a local clang8/libstdc++/linux flow, the memory usage was observed in
  // June 2019 to be 64 bytes higher than it is in CI/release. Your mileage may
  // vary.
  //
  // If you encounter a failure here, please see
  // https://github.com/envoyproxy/envoy/blob/main/source/docs/stats.md#stats-memory-tests
  // for details on how to fix.
  //
  // We only run the exact test for ipv6 because ipv4 in some cases may allocate a
  // different number of bytes. We still run the approximate test.
  if (ip_version_ != Network::Address::IpVersion::v6) {
    // https://github.com/envoyproxy/envoy/issues/12209
    // EXPECT_MEMORY_EQ(m_per_cluster, 37061);
  }
  EXPECT_MEMORY_LE(m_per_cluster, 40000); // Round up to allow platform variations.
}

TEST_P(ClusterMemoryTestRunner, MemoryLargeHostSizeWithStats) {
  // A unique instance of ClusterMemoryTest allows for multiple runs of Envoy with
  // differing configuration. This is necessary for measuring the memory consumption
  // between the different instances within the same test.
  const size_t m100 = ClusterMemoryTestHelper::computeMemoryDelta(1, 1, 1, 101, true);
  const size_t m_per_host = (m100) / 100;

  // Note: if you are increasing this golden value because you are adding a
  // stat, please confirm that this will be generally useful to most Envoy
  // users. Otherwise you are adding to the per-host memory overhead, which
  // will be significant for Envoy installations configured to talk to large
  // numbers of hosts.
  //
  // History of golden values:
  //
  // Date        PR       Bytes Per Host      Notes
  //                      exact upper-bound
  // ----------  -----    -----------------   -----
  // 2019/09/09  8189     2739         3100   Initial per-host memory snapshot
  // 2019/09/10  8216     1283         1315   Use primitive counters for host stats
  // 2019/11/01  8859     1299         1315   build: switch to libc++ by default
  // 2019/11/12  8998     1299         1350   test: adjust memory limit for macOS
  // 2019/11/15  9040     1283         1350   build: update protobuf to 3.10.1
  // 2020/01/13  9663     1619         1655   api: deprecate hosts in Cluster.
  // 2020/02/13  10042    1363         1655   Metadata object are shared across different clusters
  //                                          and hosts.
  // 2020/04/02  10624    1380         1655   Use 100 clusters rather than 1000 to avoid timeouts

  // Note: when adjusting this value: EXPECT_MEMORY_EQ is active only in CI
  // 'release' builds, where we control the platform and tool-chain. So you
  // will need to find the correct value only after failing CI and looking
  // at the logs.
  //
  // If you encounter a failure here, please see
  // https://github.com/envoyproxy/envoy/blob/main/source/docs/stats.md#stats-memory-tests
  // for details on how to fix.
  //
  // We only run the exact test for ipv6 because ipv4 in some cases may allocate a
  // different number of bytes. We still run the approximate test.
  if (ip_version_ != Network::Address::IpVersion::v6) {
    // https://github.com/envoyproxy/envoy/issues/12209
    // EXPECT_MEMORY_EQ(m_per_host, 1380);
  }
  EXPECT_MEMORY_LE(m_per_host, 2000); // Round up to allow platform variations.
}

} // namespace
} // namespace Envoy
