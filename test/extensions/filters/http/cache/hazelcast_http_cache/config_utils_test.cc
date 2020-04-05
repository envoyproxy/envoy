#include "extensions/filters/http/cache/hazelcast_http_cache/util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

using envoy::source::extensions::filters::http::cache::HazelcastHttpCacheConfig;

class ConfigUtilsTest : public testing::Test {
protected:
  uint64_t defaultPartitionSize() { return ConfigUtil::DEFAULT_PARTITION_SIZE; }
  uint64_t maxPartitionSize() { return ConfigUtil::MAX_PARTITION_SIZE; }
  uint64_t maxUnifiedBodySize() { return ConfigUtil::MAX_UNIFIED_BODY_SIZE; }
  uint64_t maxDividedBodySize() { return ConfigUtil::MAX_DIVIDED_BODY_SIZE; }
  uint32_t defaultConnectionTimeoutMs() { return ConfigUtil::DEFAULT_CONNECTION_TIMEOUT_MS; }
  uint32_t defaultConnectionAttemptLimit() { return ConfigUtil::DEFAULT_CONNECTION_ATTEMPT_LIMIT; }
  uint32_t defaultConnectionAttemptPeriodMs() {
    return ConfigUtil::DEFAULT_CONNECTION_ATTEMPT_PERIOD_MS;
  }
  uint32_t defaultInvocationTimeoutSec() { return ConfigUtil::DEFAULT_INVOCATION_TIMEOUT_SEC; }
  uint16_t partitionWarnLimit() { return ConfigUtil::PARTITION_WARN_LIMIT; }
};

TEST_F(ConfigUtilsTest, ValidPartitionSizeTest) {
  uint64_t valid_value = ConfigUtil::validPartitionSize(0);
  EXPECT_EQ(defaultPartitionSize(), valid_value);
  valid_value = ConfigUtil::validPartitionSize(maxPartitionSize() + 1);
  EXPECT_EQ(maxPartitionSize(), valid_value);
  valid_value = ConfigUtil::validPartitionSize(maxPartitionSize() - 1);
  EXPECT_EQ(maxPartitionSize() - 1, valid_value);
}

TEST_F(ConfigUtilsTest, ValidMaxBodySizeTest) {
  auto validateBodySizeTest = [this](bool unified) {
    uint64_t max_size = unified ? maxUnifiedBodySize() : maxDividedBodySize();
    uint64_t valid_value = ConfigUtil::validMaxBodySize(0, unified);
    EXPECT_EQ(max_size, valid_value);
    valid_value = ConfigUtil::validMaxBodySize(max_size + 1, unified);
    EXPECT_EQ(max_size, valid_value);
    valid_value = ConfigUtil::validMaxBodySize(max_size - 1, unified);
    EXPECT_EQ(max_size - 1, valid_value);
  };
  validateBodySizeTest(true);
  validateBodySizeTest(false);
}

TEST_F(ConfigUtilsTest, ClientConfigTest) {
  const std::string group_name = "group_foo";
  const std::string group_pass = "foo_pass";
  const std::string random_ip = "192.168.10.3";
  constexpr int random_port = 5703;

  HazelcastHttpCacheConfig default_cache_config;
  default_cache_config.set_group_name(group_name);
  default_cache_config.set_group_password(group_pass);
  HazelcastHttpCacheConfig::MemberAddress* memberAddress = default_cache_config.add_addresses();
  memberAddress->set_ip(random_ip);
  memberAddress->set_port(random_port);

  hazelcast::client::ClientConfig config = ConfigUtil::getClientConfig(default_cache_config);

  EXPECT_EQ(defaultConnectionTimeoutMs(), config.getNetworkConfig().getConnectionTimeout());
  EXPECT_EQ(defaultConnectionAttemptLimit(), config.getNetworkConfig().getConnectionAttemptLimit());
  EXPECT_EQ(defaultConnectionAttemptPeriodMs(),
            config.getNetworkConfig().getConnectionAttemptPeriod());
  EXPECT_STREQ(std::to_string(defaultInvocationTimeoutSec()).c_str(),
               config.getProperties()["hazelcast.client.invocation.timeout.seconds"].c_str());
  EXPECT_STREQ(group_name.c_str(), config.getGroupConfig().getName().c_str());
  EXPECT_STREQ(group_pass.c_str(), config.getGroupConfig().getPassword().c_str());
  std::vector<hazelcast::client::Address> addresses = config.getNetworkConfig().getAddresses();
  EXPECT_EQ(1, addresses.size());
  EXPECT_STREQ(random_ip.c_str(), addresses.at(0).getHost().c_str());
  EXPECT_EQ(random_port, addresses.at(0).getPort());
}

TEST_F(ConfigUtilsTest, WarnLimitTest) {
  EXPECT_EQ(partitionWarnLimit(), ConfigUtil::partitionWarnLimit());
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
