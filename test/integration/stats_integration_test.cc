#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"

#include "api/bootstrap.pb.h"
#include "api/stats.pb.h"
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
}

TEST_P(StatsIntegrationTest, WithDefaultTags) {
  initialize();

  auto cx_active = test_server_->gauge("listener.127.0.0.1_0.downstream_cx_active");
  EXPECT_EQ(cx_active->tags().size(), 1);
  EXPECT_EQ(cx_active->tags()[0].name_, "envoy.listener_address");
  EXPECT_EQ(cx_active->tags()[0].value_, "127.0.0.1_0");
}

TEST_P(StatsIntegrationTest, WithEmptyTagValue) {
  config_helper_.addConfigModifier([&](envoy::api::v2::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
    auto tag_specifier = bootstrap.mutable_stats_config()->mutable_stats_tags()->Add();
    tag_specifier->set_tag_name("envoy.listener_address");
  });
  initialize();

  auto cx_active = test_server_->gauge("listener.127.0.0.1_0.downstream_cx_active");
  EXPECT_EQ(cx_active->tags().size(), 1);
  EXPECT_EQ(cx_active->tags()[0].name_, "envoy.listener_address");
  EXPECT_EQ(cx_active->tags()[0].value_, "127.0.0.1_0");
}

TEST_P(StatsIntegrationTest, WithTagSpecifierWithRegex) {
  config_helper_.addConfigModifier([&](envoy::api::v2::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);
    auto tag_specifier = bootstrap.mutable_stats_config()->mutable_stats_tags()->Add();
    tag_specifier->set_tag_name("my_listener_address");
    tag_specifier->set_regex(
        "^listener\\.(((?:[_.[:digit:]]*|[_\\[\\]aAbBcCdDeEfF[:digit:]]*))\\.)");
  });
  initialize();

  auto cx_active = test_server_->gauge("listener.127.0.0.1_0.downstream_cx_active");
  EXPECT_EQ(cx_active->tags().size(), 1);
  EXPECT_EQ(cx_active->tags()[0].name_, "my_listener_address");
  EXPECT_EQ(cx_active->tags()[0].value_, "127.0.0.1_0");
}

TEST_P(StatsIntegrationTest, WithTagSpecifierWithFixedValue) {
  config_helper_.addConfigModifier([&](envoy::api::v2::Bootstrap& bootstrap) -> void {
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
