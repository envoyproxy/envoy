#include <memory>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "common/config/well_known_names.h"
#include "common/memory/stats.h"

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

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

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
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
  });
  initialize();

  auto counter = test_server_->counter("http.config_test.rq_total");
  EXPECT_EQ(counter->tags().size(), 0);
}

TEST_P(StatsIntegrationTest, WithDefaultTagExtractors) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
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
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
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
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
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
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
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
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
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
      : BaseIntegrationTest(testing::TestWithParam<Network::Address::IpVersion>::GetParam()) {}

  static size_t computeMemory(int num_clusters) {
    ClusterMemoryTestHelper helper;
    return helper.clusterMemoryHelper(num_clusters, true);
  }

private:
  /**
   * @param num_clusters number of clusters appended to bootstrap_config
   * @param allow_stats if false, enable set_reject_all in stats_config
   * @return size_t the total memory allocated
   */
  size_t clusterMemoryHelper(int num_clusters, bool allow_stats) {
    Stats::TestUtil::MemoryTest memory_test;
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      if (!allow_stats) {
        bootstrap.mutable_stats_config()->mutable_stats_matcher()->set_reject_all(true);
      }
      for (int i = 1; i < num_clusters; i++) {
        auto* c = bootstrap.mutable_static_resources()->add_clusters();
        c->set_name(fmt::format("cluster_{}", i));
      }
    });
    initialize();

    return memory_test.consumedBytes();
  }
};
class ClusterMemoryTestRunner : public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterMemoryTestRunner,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClusterMemoryTestRunner, MemoryLargeClusterSizeWithStats) {
  // A unique instance of ClusterMemoryTest allows for multiple runs of Envoy with
  // differing configuration. This is necessary for measuring the memory consumption
  // between the different instances within the same test.
  const size_t m1 = ClusterMemoryTestHelper::computeMemory(1);
  const size_t m1001 = ClusterMemoryTestHelper::computeMemory(1001);
  const size_t m_per_cluster = (m1001 - m1) / 1000;

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
  // 2019/03/20  6329     59015               Initial version
  // 2019/04/12  6477     59576               Implementing Endpoint lease...
  // 2019/04/23  6659     59512               Reintroduce dispatcher stats...
  // 2019/04/24  6161     49415               Pack tags and tag extracted names
  // 2019/05/07  6794     49957               Stats for excluded hosts in cluster
  // 2019/04/27  6733     50213               Use SymbolTable API for HTTP codes
  // 2019/05/31  6866     50157               libstdc++ upgrade in CI
  // 2019/06/03  7199     49393               absl update
  // 2019/06/06  7208     49650               make memory targets approximate
  // 2019/06/17  7243     49412       49700   macros for exact/upper-bound memory checks
  // 2019/06/29  7364     45685       46000   combine 2 levels of stat ref-counting into 1
  // 2019/06/30  7428     42742       43000   remove stats multiple inheritance, inline HeapStatData
  // 2019/07/06  7477     42742       43000   fork gauge representation to drop pending_increment_
  // 2019/07/15  7555     42806       43000   static link libstdc++ in tests
  // 2019/07/24  7503     43030       44000   add upstream filters to clusters
  // 2019/08/13  7877     42838       44000   skip EdfScheduler creation if all host weights equal

  // Note: when adjusting this value: EXPECT_MEMORY_EQ is active only in CI
  // 'release' builds, where we control the platform and tool-chain. So you
  // will need to find the correct value only after failing CI and looking
  // at the logs.
  //
  // On a local clang8/libstdc++/linux flow, the memory usage was observed in
  // June 2019 to be 64 bytes higher than it is in CI/release. Your mileage may
  // vary.
  EXPECT_MEMORY_EQ(m_per_cluster, 42838); // 104 bytes higher than a debug build.
  EXPECT_MEMORY_LE(m_per_cluster, 44000);
}

} // namespace
} // namespace Envoy
