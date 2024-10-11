#include "envoy/config/cluster/v3/cluster.pb.h"
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
  ASSERT_FALSE(dns_cluster.has_dns_jitter());
  ASSERT_FALSE(dns_cluster.has_dns_refresh_rate());
  ASSERT_FALSE(dns_cluster.has_dns_failure_refresh_rate());
  ASSERT_FALSE(dns_cluster.respect_dns_ttl());
};

TEST_F(DnsClusterBackcompatUtilTest, EmptyButSetFailureRefresRate) {
  envoy::config::cluster::v3::Cluster cluster{};

  cluster.mutable_dns_failure_refresh_rate();
  envoy::extensions::clusters::dns::v3::DnsCluster dns_cluster{};
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  ASSERT_FALSE(dns_cluster.has_dns_jitter());
  ASSERT_FALSE(dns_cluster.has_dns_refresh_rate());
  ASSERT_TRUE(dns_cluster.has_dns_failure_refresh_rate());
  ASSERT_FALSE(dns_cluster.dns_failure_refresh_rate().has_base_interval());
  ASSERT_FALSE(dns_cluster.dns_failure_refresh_rate().has_max_interval());
  ASSERT_FALSE(dns_cluster.respect_dns_ttl());
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
      )EOF");

  envoy::extensions::clusters::dns::v3::DnsCluster dns_cluster{};
  createDnsClusterFromLegacyFields(cluster, dns_cluster);
  ASSERT_EQ(dns_cluster.dns_jitter().seconds(), 1);
  ASSERT_EQ(dns_cluster.dns_jitter().nanos(), 2);
  ASSERT_EQ(dns_cluster.dns_failure_refresh_rate().base_interval().seconds(), 3);
  ASSERT_EQ(dns_cluster.dns_failure_refresh_rate().base_interval().nanos(), 4);
  ASSERT_EQ(dns_cluster.dns_failure_refresh_rate().max_interval().seconds(), 5);
  ASSERT_EQ(dns_cluster.dns_failure_refresh_rate().max_interval().nanos(), 6);
  ASSERT_EQ(dns_cluster.dns_refresh_rate().seconds(), 7);
  ASSERT_EQ(dns_cluster.dns_refresh_rate().nanos(), 8);
  ASSERT_TRUE(dns_cluster.respect_dns_ttl());
};
} // namespace Upstream
} // namespace Envoy
