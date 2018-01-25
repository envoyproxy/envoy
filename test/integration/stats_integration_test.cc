#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/metrics/v2/stats.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class StatsIntegrationTest : public BaseIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  StatsIntegrationTest() : BaseIntegrationTest(GetParam()) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override { BaseIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_CASE_P(IpVersions, StatsIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

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

} // namespace
} // namespace Envoy
