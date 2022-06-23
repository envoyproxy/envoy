#include <string>
#include <vector>

#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/cc/log_level.h"
#include "library/common/config/internal.h"

#if defined(__APPLE__)
#include "source/extensions/network/dns_resolver/apple/apple_dns_impl.h"
#endif

using testing::HasSubstr;
using testing::Not;
extern const char* alternate_protocols_cache_filter_insert;

namespace Envoy {
namespace {

using namespace Platform;

TEST(TestConfig, ConfigIsApplied) {
  auto engine_builder = EngineBuilder();
  engine_builder.addGrpcStatsDomain("asdf.fake.website")
      .addConnectTimeoutSeconds(123)
      .addDnsRefreshSeconds(456)
      .addDnsFailureRefreshSeconds(789, 987)
      .addDnsQueryTimeoutSeconds(321)
      .addDnsPreresolveHostnames("[hostname]")
      .addH2ConnectionKeepaliveIdleIntervalMilliseconds(222)
      .addH2ConnectionKeepaliveTimeoutSeconds(333)
      .addStatsFlushSeconds(654)
      .addVirtualClusters("[virtual-clusters]")
      .setAppVersion("1.2.3")
      .setAppId("1234-1234-1234")
      .setDeviceOs("probably-ubuntu-on-CI");
  auto config_str = engine_builder.generateConfigStr();

  std::vector<std::string> must_contain = {
      "- &stats_domain asdf.fake.website",
      "- &connect_timeout 123s",
      "- &dns_refresh_rate 456s",
      "- &dns_fail_base_interval 789s",
      "- &dns_fail_max_interval 987s",
      "- &dns_query_timeout 321s",
      "- &dns_preresolve_hostnames [hostname]",
      "- &h2_connection_keepalive_idle_interval 0.222s",
      "- &h2_connection_keepalive_timeout 333s",
      "- &stats_flush_interval 654s",
      "- &virtual_clusters [virtual-clusters]",
      ("- &metadata { device_os: probably-ubuntu-on-CI, "
       "app_version: 1.2.3, app_id: 1234-1234-1234 }"),
  };
  for (const auto& string : must_contain) {
    ASSERT_NE(config_str.find(string), std::string::npos) << "'" << string << "' not found";
  }
}

TEST(TestConfig, ConfigIsValid) {
  auto engine_builder = EngineBuilder();
  auto config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);

  // Test per-platform DNS fixes.
#if defined(__APPLE__)
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("envoy.network.dns_resolver.cares")));
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("envoy.network.dns_resolver.apple"));
#else
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("envoy.network.dns_resolver.cares"));
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("envoy.network.dns_resolver.apple")));
#endif
}

TEST(TestConfig, SetGzip) {
  auto engine_builder = EngineBuilder();

  engine_builder.enableGzip(false);
  auto config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("envoy.filters.http.decompressor")));

  engine_builder.enableGzip(true);
  config_str = engine_builder.generateConfigStr();
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("envoy.filters.http.decompressor"));
}

TEST(TestConfig, SetBrotli) {
  auto engine_builder = EngineBuilder();

  engine_builder.enableBrotli(false);
  auto config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("brotli.decompressor.v3.Brotli")));

  engine_builder.enableBrotli(true);
  config_str = engine_builder.generateConfigStr();
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("brotli.decompressor.v3.Brotli"));
}

TEST(TestConfig, SetAltSvcCache) {
  auto engine_builder = EngineBuilder();

  std::string config_str = absl::StrCat(config_header, engine_builder.generateConfigStr());

  absl::StrReplaceAll({{"#{custom_filters}", alternate_protocols_cache_filter_insert}},
                      &config_str);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(config_str, bootstrap);
}

TEST(TestConfig, RemainingTemplatesThrows) {
  auto engine_builder = EngineBuilder("{{ template_that_i_will_not_fill }}");
  try {
    engine_builder.generateConfigStr();
    FAIL() << "Expected std::runtime_error";
  } catch (std::runtime_error& err) {
    EXPECT_EQ(err.what(), std::string("could not resolve all template keys in config"));
  }
}

} // namespace
} // namespace Envoy
