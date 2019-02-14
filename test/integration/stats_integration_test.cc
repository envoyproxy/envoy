#include <memory>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/metrics/v2/stats.pb.h"

#include "common/config/well_known_names.h"
#include "common/memory/stats.h"

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

class ClusterMemoryUtilization : public TestBaseWithParam<Network::Address::IpVersion> {
public:
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
    tag_specifier->set_regex("^(?:|listener(?=\\.).*?\\.)http\\.((.*?)\\.)");
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

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterMemoryUtilization,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

//uint64_t memoryConsumedWithClusters(int num_clusters, bool allow_stats) {
//    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
//        if (!allow_stats) {
//            bootstrap.mutable_stats_config()->mutable_stats_matcher()->set_reject_all(true);
//        }
//        for (int i = 1; i < num_clusters; i++) {
//            auto* c = bootstrap.mutable_static_resources()->add_clusters();
//            c->set_name(fmt::format("cluster_{}", i));
//        }
//    });
//
//};

// TODO(cmluciano) Refactor once envoyproxy/envoy#5624 is solved
// TODO(cmluciano) Add options to measure multiple workers
TEST_P(ClusterMemoryUtilization, MemoryLargeClusterSizeWithStats) {
    const size_t start_mem = Memory::Stats::totalCurrentlyAllocated() / 1000;
    if (start_mem == 0) {
        // Skip this test for platforms where we can't measure memory.
        return;
    }

    auto IpVersions = TestBaseWithParam<Network::Address::IpVersion>::GetParam();

    // HTTP_PROXY_CONFIG with 1 cluster
    auto* t1 = new BaseIntegrationTest(IpVersions);
    t1->initialize();
    size_t m1 = Memory::Stats::totalCurrentlyAllocated();
    EXPECT_LT(start_mem, m1);
    EXPECT_EQ(m1, 57000);
    delete t1;

    // HTTP_PROXY_CONFIG with 1001 clusters
    std::string config = ConfigHelper::HTTP_PROXY_CONFIG;
    auto* t2 = new BaseIntegrationTest(IpVersions,config);
//    t2->config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
//        for (int i = 1; i < 1001; i++) {
//            auto* c = bootstrap.mutable_static_resources()->add_clusters();
//            c->set_name(fmt::format("cluster_{}", i));
//        }
//    });
    t2->initialize();
    size_t m2 = Memory::Stats::totalCurrentlyAllocated();
    EXPECT_LT(start_mem, m2);
    EXPECT_EQ(m2, 57000);
    size_t m_per_cluster = (m2 - m1) / 1000;
    EXPECT_EQ(m_per_cluster, 57000);
    delete t2;
}

} // namespace
} // namespace Envoy
