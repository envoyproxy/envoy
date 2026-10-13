#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/common/dns/v3/dns.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"

#include "source/extensions/clusters/common/dns_cluster_backcompat.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class DnsClusterBackcompatUtilTest : public testing::Test {};

TEST_F(DnsClusterBackcompatUtilTest, Empty) {
  envoy::config::cluster::v3::Cluster cluster{};
  envoy::extensions::clusters::dns::v3::DnsCluster dns_cluster{};
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_FALSE(dns_cluster.has_dns_jitter());
  EXPECT_FALSE(dns_cluster.has_dns_refresh_rate());
  EXPECT_FALSE(dns_cluster.has_dns_failure_refresh_rate());
  EXPECT_FALSE(dns_cluster.respect_dns_ttl());
  EXPECT_TRUE(dns_cluster.dns_lookup_family() ==
              envoy::extensions::clusters::common::dns::v3::AUTO);
  EXPECT_FALSE(dns_cluster.has_typed_dns_resolver_config());
};

TEST_F(DnsClusterBackcompatUtilTest, EmptyButSetFailureRefreshRate) {
  envoy::config::cluster::v3::Cluster cluster{};

  cluster.mutable_dns_failure_refresh_rate();
  envoy::extensions::clusters::dns::v3::DnsCluster dns_cluster{};
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_FALSE(dns_cluster.has_dns_jitter());
  EXPECT_FALSE(dns_cluster.has_dns_refresh_rate());
  EXPECT_TRUE(dns_cluster.has_dns_failure_refresh_rate());
  EXPECT_FALSE(dns_cluster.dns_failure_refresh_rate().has_base_interval());
  EXPECT_FALSE(dns_cluster.dns_failure_refresh_rate().has_max_interval());
  EXPECT_FALSE(dns_cluster.respect_dns_ttl());
  EXPECT_TRUE(dns_cluster.dns_lookup_family() ==
              envoy::extensions::clusters::common::dns::v3::AUTO);
  EXPECT_FALSE(dns_cluster.has_typed_dns_resolver_config());
};

TEST_F(DnsClusterBackcompatUtilTest, FullClusterConfig) {
  envoy::config::cluster::v3::Cluster cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(R"EOF(
          type: STRICT_DNS
          dns_jitter:
            seconds: 1
            nanos: 2
          dns_failure_refresh_rate:
            base_interval:
              seconds: 3
              nanos: 4
            max_interval:
              seconds: 5
              nanos: 6
          dns_refresh_rate:
            seconds: 7
            nanos: 8
          respect_dns_ttl: true
          dns_lookup_family: V6_ONLY
      )EOF");

  envoy::extensions::clusters::dns::v3::DnsCluster dns_cluster{};
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_EQ(dns_cluster.dns_jitter().seconds(), 1);
  EXPECT_EQ(dns_cluster.dns_jitter().nanos(), 2);
  EXPECT_EQ(dns_cluster.dns_failure_refresh_rate().base_interval().seconds(), 3);
  EXPECT_EQ(dns_cluster.dns_failure_refresh_rate().base_interval().nanos(), 4);
  EXPECT_EQ(dns_cluster.dns_failure_refresh_rate().max_interval().seconds(), 5);
  EXPECT_EQ(dns_cluster.dns_failure_refresh_rate().max_interval().nanos(), 6);
  EXPECT_EQ(dns_cluster.dns_refresh_rate().seconds(), 7);
  EXPECT_EQ(dns_cluster.dns_refresh_rate().nanos(), 8);
  EXPECT_TRUE(dns_cluster.respect_dns_ttl());
  EXPECT_TRUE(dns_cluster.dns_lookup_family() ==
              envoy::extensions::clusters::common::dns::v3::V6_ONLY);
  EXPECT_FALSE(dns_cluster.has_typed_dns_resolver_config());
};

TEST_F(DnsClusterBackcompatUtilTest, LookupFamilyTranslation) {
  envoy::config::cluster::v3::Cluster cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(R"EOF(
          type: STRICT_DNS
          dns_lookup_family: V6_ONLY
      )EOF");
  envoy::extensions::clusters::dns::v3::DnsCluster dns_cluster{};
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_TRUE(dns_cluster.dns_lookup_family() ==
              envoy::extensions::clusters::common::dns::v3::V6_ONLY);

  cluster = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(R"EOF(
          type: STRICT_DNS
          dns_lookup_family: V4_ONLY
      )EOF");
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_TRUE(dns_cluster.dns_lookup_family() ==
              envoy::extensions::clusters::common::dns::v3::V4_ONLY);

  cluster = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(R"EOF(
          type: STRICT_DNS
          dns_lookup_family: AUTO
      )EOF");
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_TRUE(dns_cluster.dns_lookup_family() ==
              envoy::extensions::clusters::common::dns::v3::AUTO);

  cluster = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(R"EOF(
          type: STRICT_DNS
          dns_lookup_family: V4_PREFERRED
      )EOF");
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_TRUE(dns_cluster.dns_lookup_family() ==
              envoy::extensions::clusters::common::dns::v3::V4_PREFERRED);

  cluster = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(R"EOF(
          type: STRICT_DNS
          dns_lookup_family: ALL
      )EOF");
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  EXPECT_TRUE(dns_cluster.dns_lookup_family() == envoy::extensions::clusters::common::dns::v3::ALL);
}
} // namespace Upstream
} // namespace Envoy
