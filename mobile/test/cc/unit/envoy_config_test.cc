#include <string>
#include <vector>

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/cc/log_level.h"

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
      "- &stats_flush_interval 654s",
      "- &virtual_clusters [virtual-clusters]",
      ("- &metadata { device_os: probably-ubuntu-on-CI, "
       "app_version: 1.2.3, app_id: 1234-1234-1234 }"),
  };
  for (const auto& string : must_contain) {
    ASSERT_NE(config_str.find(string), std::string::npos) << "'" << string << "' not found";
  }
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
