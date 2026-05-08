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

  // The default Happy Eyeballs configuration used if not specified on cluster.
  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig config;
  config.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::DEFAULT);
  config.mutable_first_address_family_count()->set_value(1);

  // All v4 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v4_list = {ip_v4_1, ip_v4_2, ip_v4_3, ip_v4_4};
  EXPECT_EQ(v4_list, HappyEyeballsConnectionProvider::sortAddresses(v4_list, config));

  // All v6 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v6_list = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v6_4};
  EXPECT_EQ(v6_list, HappyEyeballsConnectionProvider::sortAddresses(v6_list, config));

  std::vector<Address::InstanceConstSharedPtr> v6_then_v4 = {ip_v6_1, ip_v6_2, ip_v4_1, ip_v4_2};
  std::vector<Address::InstanceConstSharedPtr> interleaved = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2};
  EXPECT_EQ(interleaved, HappyEyeballsConnectionProvider::sortAddresses(v6_then_v4, config));

  std::vector<Address::InstanceConstSharedPtr> v6_then_single_v4 = {ip_v6_1, ip_v6_2, ip_v6_3,
                                                                    ip_v4_1};
  std::vector<Address::InstanceConstSharedPtr> interleaved2 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v6_3};
  EXPECT_EQ(interleaved2,
            HappyEyeballsConnectionProvider::sortAddresses(v6_then_single_v4, config));

  std::vector<Address::InstanceConstSharedPtr> mixed = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v4_1,
                                                        ip_v4_2, ip_v4_3, ip_v4_4, ip_v6_4};
  std::vector<Address::InstanceConstSharedPtr> interleaved3 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2,
                                                               ip_v6_3, ip_v4_3, ip_v6_4, ip_v4_4};
  EXPECT_EQ(interleaved3, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));
}

TEST_F(HappyEyeballsConnectionProviderTest, SortAddressesWithFirstAddressFamilyCount) {
  auto ip_v4_1 = std::make_shared<Address::Ipv4Instance>("127.0.0.1");
  auto ip_v4_2 = std::make_shared<Address::Ipv4Instance>("127.0.0.2");
  auto ip_v4_3 = std::make_shared<Address::Ipv4Instance>("127.0.0.3");
  auto ip_v4_4 = std::make_shared<Address::Ipv4Instance>("127.0.0.4");

  auto ip_v6_1 = std::make_shared<Address::Ipv6Instance>("ff02::1", 0);
  auto ip_v6_2 = std::make_shared<Address::Ipv6Instance>("ff02::2", 0);
  auto ip_v6_3 = std::make_shared<Address::Ipv6Instance>("ff02::3", 0);
  auto ip_v6_4 = std::make_shared<Address::Ipv6Instance>("ff02::4", 0);

  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig config;
  config.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::V4);
  config.mutable_first_address_family_count()->set_value(2);

  // All v4 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v4_list = {ip_v4_1, ip_v4_2, ip_v4_3, ip_v4_4};
  EXPECT_EQ(v4_list, HappyEyeballsConnectionProvider::sortAddresses(v4_list, config));

  // All v6 address so unchanged.
  std::vector<Address::InstanceConstSharedPtr> v6_list = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v6_4};
  EXPECT_EQ(v6_list, HappyEyeballsConnectionProvider::sortAddresses(v6_list, config));

  // v6 then v4, return interleaved list.
  std::vector<Address::InstanceConstSharedPtr> v6_then_v4 = {ip_v6_1, ip_v4_1, ip_v6_2, ip_v4_2};
  std::vector<Address::InstanceConstSharedPtr> interleaved2 = {ip_v4_1, ip_v4_2, ip_v6_1, ip_v6_2};
  EXPECT_EQ(interleaved2, HappyEyeballsConnectionProvider::sortAddresses(v6_then_v4, config));

  // v6 then single v4, return v4 first interleaved list.
  std::vector<Address::InstanceConstSharedPtr> v6_then_single_v4 = {ip_v6_1, ip_v6_2, ip_v6_3,
                                                                    ip_v4_1};
  std::vector<Address::InstanceConstSharedPtr> interleaved = {ip_v4_1, ip_v6_1, ip_v6_2, ip_v6_3};
  EXPECT_EQ(interleaved, HappyEyeballsConnectionProvider::sortAddresses(v6_then_single_v4, config));

  // mixed
  std::vector<Address::InstanceConstSharedPtr> mixed = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v4_1,
                                                        ip_v4_2, ip_v4_3, ip_v4_4, ip_v6_4};
  std::vector<Address::InstanceConstSharedPtr> interleaved3 = {ip_v4_1, ip_v4_2, ip_v6_1, ip_v4_3,
                                                               ip_v4_4, ip_v6_2, ip_v6_3, ip_v6_4};
  EXPECT_EQ(interleaved3, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // missing first_address_family_version
  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig config_no_version;
  config_no_version.mutable_first_address_family_count()->set_value(2);
  // first_address_family_version should default to DEFAULT when absent.
  // v6 then v4, return interleaved list.
  std::vector<Address::InstanceConstSharedPtr> interleaved4 = {ip_v6_1, ip_v6_2, ip_v4_1, ip_v4_2};
  EXPECT_EQ(interleaved4,
            HappyEyeballsConnectionProvider::sortAddresses(v6_then_v4, config_no_version));

  // missing first_address_family_count
  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig config_no_count;
  config_no_count.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::V4);
  // first_address_family_count should default to 1 when absent.
  // v6 then v4, return interleaved list.
  std::vector<Address::InstanceConstSharedPtr> interleaved5 = {ip_v4_1, ip_v6_1, ip_v4_2, ip_v6_2};
  EXPECT_EQ(interleaved5,
            HappyEyeballsConnectionProvider::sortAddresses(v6_then_v4, config_no_count));
}

TEST_F(HappyEyeballsConnectionProviderTest, SortAddressesWithNonIpFamilies) {
  auto ip_v4_1 = std::make_shared<Address::Ipv4Instance>("127.0.0.1");
  auto ip_v4_2 = std::make_shared<Address::Ipv4Instance>("127.0.0.2");
  auto ip_v4_3 = std::make_shared<Address::Ipv4Instance>("127.0.0.3");
  auto ip_v4_4 = std::make_shared<Address::Ipv4Instance>("127.0.0.4");

  auto ip_v6_1 = std::make_shared<Address::Ipv6Instance>("ff02::1", 0);
  auto ip_v6_2 = std::make_shared<Address::Ipv6Instance>("ff02::2", 0);
  auto ip_v6_3 = std::make_shared<Address::Ipv6Instance>("ff02::3", 0);
  auto ip_v6_4 = std::make_shared<Address::Ipv6Instance>("ff02::4", 0);

  Address::InstanceConstSharedPtr pipe_1 = Address::PipeInstance::create("/tmp/pipe1").value();
  Address::InstanceConstSharedPtr pipe_2 = Address::PipeInstance::create("/tmp/pipe2").value();
  Address::InstanceConstSharedPtr pipe_3 = Address::PipeInstance::create("/tmp/pipe3").value();
  Address::InstanceConstSharedPtr pipe_4 = Address::PipeInstance::create("/tmp/pipe4").value();

  auto internal_1 = std::make_shared<Address::EnvoyInternalInstance>("internal1");
  auto internal_2 = std::make_shared<Address::EnvoyInternalInstance>("internal2");
  auto internal_3 = std::make_shared<Address::EnvoyInternalInstance>("internal3");
  auto internal_4 = std::make_shared<Address::EnvoyInternalInstance>("internal4");

  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig config;
  config.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::DEFAULT);
  config.mutable_first_address_family_count()->set_value(1);

  // All the same address type so unchanged.
  std::vector<Address::InstanceConstSharedPtr> pipe_list = {pipe_1, pipe_2, pipe_3, pipe_4};
  EXPECT_EQ(pipe_list, HappyEyeballsConnectionProvider::sortAddresses(pipe_list, config));

  // All the same address type so unchanged.
  std::vector<Address::InstanceConstSharedPtr> internal_list = {internal_1, internal_2, internal_3,
                                                                internal_4};
  EXPECT_EQ(internal_list, HappyEyeballsConnectionProvider::sortAddresses(internal_list, config));

  // Interleave EnvoyInternal and IPv6 addresses.
  std::vector<Address::InstanceConstSharedPtr> mixed = {
      ip_v6_1, ip_v6_2, ip_v6_3, ip_v6_4, internal_1, internal_2, internal_3, internal_4};
  std::vector<Address::InstanceConstSharedPtr> expected = {
      ip_v6_1, internal_1, ip_v6_2, internal_2, ip_v6_3, internal_3, ip_v6_4, internal_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Interleave three address types.
  mixed = {ip_v6_1, ip_v6_2, pipe_1, ip_v6_3, pipe_2, internal_1, internal_2, internal_3, pipe_3};
  expected = {ip_v6_1,    pipe_1,  internal_1, ip_v6_2,   pipe_2,
              internal_2, ip_v6_3, pipe_3,     internal_3};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Four of two address types, then one of a third.
  mixed = {ip_v6_1, ip_v6_2, ip_v6_3, ip_v6_4, ip_v4_1, ip_v4_2, ip_v4_3, ip_v4_4, internal_1};
  expected = {ip_v6_1, ip_v4_1, internal_1, ip_v6_2, ip_v4_2, ip_v6_3, ip_v4_3, ip_v6_4, ip_v4_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Interleave all four address types.
  mixed = {ip_v6_1,    ip_v6_2,    ip_v4_1, pipe_1,     pipe_2,  pipe_3,  ip_v4_2, internal_1,
           internal_2, internal_3, ip_v4_3, internal_4, ip_v6_3, ip_v6_4, ip_v4_4, pipe_4};
  expected = {ip_v6_1, ip_v4_1, pipe_1, internal_1, ip_v6_2, ip_v4_2, pipe_2, internal_2,
              ip_v6_3, ip_v4_3, pipe_3, internal_3, ip_v6_4, ip_v4_4, pipe_4, internal_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Non-IP address as first address.
  mixed = {internal_1, internal_2, internal_3, ip_v4_1, ip_v4_2,
           ip_v6_1,    ip_v4_3,    ip_v6_2,    ip_v6_3};
  expected = {internal_1, ip_v4_1,    ip_v6_1, internal_2, ip_v4_2,
              ip_v6_2,    internal_3, ip_v4_3, ip_v6_3};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Change preferred to IPv4, count=2.
  config.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::V4);
  config.mutable_first_address_family_count()->set_value(2);

  // Interleave all four address types with IPv4 preferred (count = 2). We run out of IPv4
  // addresses before the count is reached, so the remaining addresses are interleaved
  // according to the order their families appear in the input.
  mixed = {ip_v6_1,    ip_v6_2,    ip_v4_1,    pipe_1,  pipe_2,     pipe_3,  ip_v4_2,
           internal_1, internal_2, internal_3, ip_v4_3, internal_4, ip_v6_3, ip_v4_4};
  expected = {ip_v4_1, ip_v4_2, ip_v6_1,    pipe_1,  internal_1, ip_v4_3,    ip_v4_4,
              ip_v6_2, pipe_2,  internal_2, ip_v6_3, pipe_3,     internal_3, internal_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Test with three IPv4 addresses; we run out of IPv4 addresses while filling
  // first_address_family_count for the second time.
  mixed = {ip_v6_1,    ip_v6_2,    ip_v4_1,    ip_v4_2,    ip_v4_3,
           internal_1, internal_2, internal_3, internal_4, ip_v6_3};
  expected = {ip_v4_1, ip_v4_2,    ip_v6_1, internal_1, ip_v4_3,
              ip_v6_2, internal_2, ip_v6_3, internal_3, internal_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // There are fewer of the preferred family than first_address_family_count.
  mixed = {ip_v6_1, ip_v6_2, ip_v4_1, internal_1, internal_2, internal_3, internal_4, ip_v6_3};
  expected = {ip_v4_1, ip_v6_1, internal_1, ip_v6_2, internal_2, ip_v6_3, internal_3, internal_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // There are no addresses of the preferred family. The rest gets interleaved in the order their
  // families appear in the input.
  mixed = {ip_v6_1,    ip_v6_2,    pipe_1,     pipe_2, internal_1,
           internal_2, internal_3, internal_4, ip_v6_3};
  expected = {ip_v6_1,    pipe_1,  internal_1, ip_v6_2,   pipe_2,
              internal_2, ip_v6_3, internal_3, internal_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Prefer EnvoyInternal addresses
  config.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::INTERNAL);
  config.mutable_first_address_family_count()->set_value(3);

  mixed = {ip_v6_1, ip_v6_2, pipe_1, ip_v6_3, pipe_2, internal_1, internal_2, internal_3, pipe_3};
  expected = {internal_1, internal_2, internal_3, ip_v6_1, pipe_1,
              ip_v6_2,    pipe_2,     ip_v6_3,    pipe_3};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Prefer EnvoyInternal addresses, but there are none.
  mixed = {ip_v6_1, ip_v6_2, pipe_1, ip_v6_3, pipe_2, pipe_3, pipe_4};
  expected = {ip_v6_1, pipe_1, ip_v6_2, pipe_2, ip_v6_3, pipe_3, pipe_4};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));

  // Prefer Pipe addresses.
  config.set_first_address_family_version(
      envoy::config::cluster::v3::UpstreamConnectionOptions::PIPE);
  config.mutable_first_address_family_count()->set_value(3);

  mixed = {ip_v6_1, ip_v6_2, pipe_1, ip_v6_3, pipe_2, internal_1, internal_2, internal_3, pipe_3};
  expected = {pipe_1,  pipe_2,     pipe_3,  ip_v6_1,   internal_1,
              ip_v6_2, internal_2, ip_v6_3, internal_3};
  EXPECT_EQ(expected, HappyEyeballsConnectionProvider::sortAddresses(mixed, config));
}

} // namespace Network
} // namespace Envoy
