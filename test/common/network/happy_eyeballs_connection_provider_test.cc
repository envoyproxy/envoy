#include <cstddef>

#include "envoy/network/address.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/happy_eyeballs_connection_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

class HappyEyeballsConnectionProviderTest : public testing::Test {};

TEST_F(HappyEyeballsConnectionProviderTest, SortAddresses) {
  auto ip_v4_1 = std::make_shared<Address::Ipv4Instance>("127.0.0.1");
  auto ip_v4_2 = std::make_shared<Address::Ipv4Instance>("127.0.0.2");
  auto ip_v4_3 = std::make_shared<Address::Ipv4Instance>("127.0.0.3");
  auto ip_v4_4 = std::make_shared<Address::Ipv4Instance>("127.0.0.4");

  auto ip_v6_1 = std::make_shared<Address::Ipv6Instance>("ff02::1", 0);
  auto ip_v6_2 = std::make_shared<Address::Ipv6Instance>("ff02::2", 0);
  auto ip_v6_3 = std::make_shared<Address::Ipv6Instance>("ff02::3", 0);
  auto ip_v6_4 = std::make_shared<Address::Ipv6Instance>("ff02::4", 0);

  // All v4 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v4_list = {ip_v4_1, ip_v4_2, ip_v4_3, ip_v4_4};
  EXPECT_EQ(v4_list, HappyEyeballsConnectionProvider::sortAddresses(v4_list));

  // All v6 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v6_list = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v6_4};
  EXPECT_EQ(v6_list, HappyEyeballsConnectionProvider::sortAddresses(v6_list));

  std::vector<Address::InstanceConstSharedPtr> v6_then_v4 = {ip_v6_1, ip_v6_2, ip_v4_1, ip_v4_2};
  std::vector<Address::InstanceConstSharedPtr> interleaved = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2};
  EXPECT_EQ(interleaved, HappyEyeballsConnectionProvider::sortAddresses(v6_then_v4));

  std::vector<Address::InstanceConstSharedPtr> v6_then_single_v4 = {ip_v6_1, ip_v6_2, ip_v6_3,
                                                                    ip_v4_1};
  std::vector<Address::InstanceConstSharedPtr> interleaved2 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v6_3};
  EXPECT_EQ(interleaved2, HappyEyeballsConnectionProvider::sortAddresses(v6_then_single_v4));

  std::vector<Address::InstanceConstSharedPtr> mixed = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v4_1,
                                                        ip_v4_2, ip_v4_3, ip_v4_4, ip_v6_4};
  std::vector<Address::InstanceConstSharedPtr> interleaved3 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2,
                                                               ip_v6_3, ip_v4_3, ip_v6_4, ip_v4_4};
  EXPECT_EQ(interleaved3, HappyEyeballsConnectionProvider::sortAddresses(mixed));
}

TEST_F(HappyEyeballsConnectionProviderTest, SortAddressesWithHappyEyeballsConfig) {
  auto ip_v4_1 = std::make_shared<Address::Ipv4Instance>("127.0.0.1");
  auto ip_v4_2 = std::make_shared<Address::Ipv4Instance>("127.0.0.2");
  auto ip_v4_3 = std::make_shared<Address::Ipv4Instance>("127.0.0.3");
  auto ip_v4_4 = std::make_shared<Address::Ipv4Instance>("127.0.0.4");

  auto ip_v6_1 = std::make_shared<Address::Ipv6Instance>("ff02::1", 0);
  auto ip_v6_2 = std::make_shared<Address::Ipv6Instance>("ff02::2", 0);
  auto ip_v6_3 = std::make_shared<Address::Ipv6Instance>("ff02::3", 0);
  auto ip_v6_4 = std::make_shared<Address::Ipv6Instance>("ff02::4", 0);

  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig he_config;
  he_config.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::V4);
  he_config.mutable_first_address_family_count()->set_value(2);
  auto config = absl::make_optional(he_config);

  // All v4 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v4_list = {ip_v4_1, ip_v4_2, ip_v4_3, ip_v4_4};
  EXPECT_EQ(v4_list, HappyEyeballsConnectionProvider::sortAddressesWithConfig(v4_list, config));

  // All v6 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v6_list = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v6_4};
  EXPECT_EQ(v6_list, HappyEyeballsConnectionProvider::sortAddressesWithConfig(v6_list, config));

  // v6 then v4, return interleaved list.
  std::vector<Address::InstanceConstSharedPtr> v6_then_v4 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2};
  std::vector<Address::InstanceConstSharedPtr> interleaved2 = {ip_v4_1, ip_v4_2, ip_v6_1, ip_v6_2};
  EXPECT_EQ(interleaved2,
            HappyEyeballsConnectionProvider::sortAddressesWithConfig(v6_then_v4, config));

  // v6 then single v4, return v4 first interleaved list.
  std::vector<Address::InstanceConstSharedPtr> v6_then_single_v4 = {ip_v6_1, ip_v6_2, ip_v6_3,
                                                                    ip_v4_1};
  std::vector<Address::InstanceConstSharedPtr> interleaved = {ip_v4_1, ip_v6_1, ip_v6_2, ip_v6_3};
  EXPECT_EQ(interleaved,
            HappyEyeballsConnectionProvider::sortAddressesWithConfig(v6_then_single_v4, config));

  // mixed
  std::vector<Address::InstanceConstSharedPtr> mixed = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v4_1,
                                                        ip_v4_2, ip_v4_3, ip_v4_4, ip_v6_4};
  std::vector<Address::InstanceConstSharedPtr> interleaved3 = {ip_v4_1, ip_v4_2, ip_v6_1, ip_v4_3,
                                                               ip_v4_4, ip_v6_2, ip_v6_3, ip_v6_4};
  EXPECT_EQ(interleaved3, HappyEyeballsConnectionProvider::sortAddressesWithConfig(mixed, config));

  // missing first_address_family_version
  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig he_config_no_version;
  he_config_no_version.mutable_first_address_family_count()->set_value(2);
  auto config_no_version = absl::make_optional(he_config_no_version);
  // first_address_family_version should default to DEFAULT when absent.
  // v6 then v4, return interleaved list.
  std::vector<Address::InstanceConstSharedPtr> interleaved4 = {ip_v6_1, ip_v6_2, ip_v4_1, ip_v4_2};
  EXPECT_EQ(interleaved4, HappyEyeballsConnectionProvider::sortAddressesWithConfig(
                              v6_then_v4, config_no_version));

  // missing first_address_family_count
  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig he_config_no_count;
  he_config_no_count.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::V4);
  auto config_no_count = absl::make_optional(he_config_no_count);
  // first_address_family_count should default to 1 when absent.
  // v6 then v4, return interleaved list.
  std::vector<Address::InstanceConstSharedPtr> interleaved5 = {ip_v4_1, ip_v6_1, ip_v4_2, ip_v6_2};
  EXPECT_EQ(interleaved5,
            HappyEyeballsConnectionProvider::sortAddressesWithConfig(v6_then_v4, config_no_count));
}

} // namespace Network
} // namespace Envoy
