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
  EngineBuilder engine_builder;
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
  std::string config_str = engine_builder.generateConfigStr();

  std::vector<std::string> must_contain = {"- &stats_domain asdf.fake.website",
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
                                           R"(- &validation_context
  trusted_ca:)"};
  for (const auto& string : must_contain) {
    ASSERT_NE(config_str.find(string), std::string::npos) << "'" << string << "' not found";
  }
}

TEST(TestConfig, ConfigIsValid) {
  EngineBuilder engine_builder;
  std::string config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);

  // Test per-platform DNS fixes.
#if defined(__APPLE__)
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("envoy.network.dns_resolver.getaddrinfo")));
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("envoy.network.dns_resolver.apple"));
#else
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("envoy.network.dns_resolver.getaddrinfo"));
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("envoy.network.dns_resolver.apple")));
#endif
}

#if !defined(__APPLE__)
TEST(TestConfig, SetUseDnsCAresResolver) {
  EngineBuilder engine_builder;
  engine_builder.useDnsSystemResolver(false);
  std::string config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);

  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("envoy.network.dns_resolver.cares"));
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("envoy.network.dns_resolver.getaddrinfo")));
}
#endif

TEST(TestConfig, SetGzip) {
  EngineBuilder engine_builder;

  engine_builder.enableGzip(false);
  std::string config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("envoy.filters.http.decompressor")));

  engine_builder.enableGzip(true);
  config_str = engine_builder.generateConfigStr();
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("envoy.filters.http.decompressor"));
}

TEST(TestConfig, SetBrotli) {
  EngineBuilder engine_builder;

  engine_builder.enableBrotli(false);
  std::string config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("brotli.decompressor.v3.Brotli")));

  engine_builder.enableBrotli(true);
  config_str = engine_builder.generateConfigStr();
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("brotli.decompressor.v3.Brotli"));
}

TEST(TestConfig, SetSocketTag) {
  EngineBuilder engine_builder;

  engine_builder.enableSocketTagging(false);
  std::string config_str = engine_builder.generateConfigStr();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("http.socket_tag.SocketTag")));

  engine_builder.enableSocketTagging(true);
  config_str = engine_builder.generateConfigStr();
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("http.socket_tag.SocketTag"));
}

TEST(TestConfig, SetAltSvcCache) {
  EngineBuilder engine_builder;

  std::string config_str = absl::StrCat(config_header, engine_builder.generateConfigStr());

  absl::StrReplaceAll({{"#{custom_filters}", alternate_protocols_cache_filter_insert}},
                      &config_str);

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(config_str, bootstrap);
}

TEST(TestConfig, StreamIdleTimeout) {
  EngineBuilder engine_builder;

  std::string config_str = absl::StrCat(config_header, engine_builder.generateConfigStr());
  ASSERT_THAT(config_str, HasSubstr("&stream_idle_timeout 15s"));
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(config_str, bootstrap);

  engine_builder.setStreamIdleTimeoutSeconds(42);
  config_str = absl::StrCat(config_header, engine_builder.generateConfigStr());
  ASSERT_THAT(config_str, HasSubstr("&stream_idle_timeout 42s"));
  TestUtility::loadFromYaml(config_str, bootstrap);
}

TEST(TestConfig, PerTryIdleTimeout) {
  EngineBuilder engine_builder;

  std::string config_str = absl::StrCat(config_header, engine_builder.generateConfigStr());
  ASSERT_THAT(config_str, HasSubstr("&per_try_idle_timeout 15s"));
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(config_str, bootstrap);

  engine_builder.setPerTryIdleTimeoutSeconds(42);
  config_str = absl::StrCat(config_header, engine_builder.generateConfigStr());
  ASSERT_THAT(config_str, HasSubstr("&per_try_idle_timeout 42s"));
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

TEST(TestConfig, EnablePlatformCertificatesValidation) {
  auto engine_builder = EngineBuilder();
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  engine_builder.enablePlatformCertificatesValidation(false);
  auto config_str1 = engine_builder.generateConfigStr();
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str1), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(),
              Not(HasSubstr("envoy_mobile.cert_validator.platform_bridge_cert_validator")));
  ASSERT_THAT(bootstrap.DebugString(), HasSubstr("trusted_ca"));

#if not defined(__APPLE__)
  engine_builder.enablePlatformCertificatesValidation(true);
  auto config_str2 = engine_builder.generateConfigStr();
  TestUtility::loadFromYaml(absl::StrCat(config_header, config_str2), bootstrap);
  ASSERT_THAT(bootstrap.DebugString(),
              HasSubstr("envoy_mobile.cert_validator.platform_bridge_cert_validator"));
  ASSERT_THAT(bootstrap.DebugString(), Not(HasSubstr("trusted_ca")));
#else
  EXPECT_DEATH(engine_builder.enablePlatformCertificatesValidation(true),
               "Certificates validation using platform provided APIs is not supported in IOS");
#endif
}

} // namespace
} // namespace Envoy
