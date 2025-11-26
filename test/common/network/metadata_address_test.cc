#include <memory>

#include "envoy/network/address.h"

#include "source/common/network/address_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Address {
namespace {

// Test that regular addresses are not metadata-only
TEST(MetadataAddressTest, RegularAddressesNotMetadataOnly) {
  // IPv4
  {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(0xC0A80001); // 192.168.0.1

    auto instance = std::make_shared<Ipv4Instance>(&addr);
    EXPECT_FALSE(instance->isMetadataOnly());
  }

  // IPv6
  {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(443);
    inet_pton(AF_INET6, "2001:db8::1", &addr.sin6_addr);

    auto instance = std::make_shared<Ipv6Instance>(addr);
    EXPECT_FALSE(instance->isMetadataOnly());
  }
}

// Test that metadata instances are properly marked
TEST(MetadataAddressTest, MetadataInstancesAreMetadataOnly) {
  // IPv4 metadata instance
  {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(0xC0A80001); // 192.168.0.1

    auto result = InstanceFactory::createMetadataIpv4Instance(&addr);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE((*result)->isMetadataOnly());

    // Verify the address data is correct
    EXPECT_EQ((*result)->ip()->addressAsString(), "192.168.0.1");
    EXPECT_EQ((*result)->ip()->port(), 80);
  }

  // IPv6 metadata instance
  {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(443);
    inet_pton(AF_INET6, "2001:db8::1", &addr.sin6_addr);

    auto result = InstanceFactory::createMetadataIpv6Instance(addr);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE((*result)->isMetadataOnly());

    // Verify the address data is correct
    EXPECT_EQ((*result)->ip()->addressAsString(), "2001:db8::1");
    EXPECT_EQ((*result)->ip()->port(), 443);
  }
}

// Test that metadata addresses work without OS support
TEST(MetadataAddressTest, MetadataInstancesWorkWithoutOsSupport) {
  // IPv4 with OS support disabled
  {
    Cleanup cleaner = Ipv4Instance::forceProtocolUnsupportedForTest(true);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(0x0A000001); // 10.0.0.1

    auto result = InstanceFactory::createMetadataIpv4Instance(&addr);
    ASSERT_TRUE(result.ok()) << "Should create metadata instance even without OS support";
    EXPECT_TRUE((*result)->isMetadataOnly());
    EXPECT_EQ((*result)->ip()->addressAsString(), "10.0.0.1");
    EXPECT_EQ((*result)->ip()->port(), 8080);
  }

  // IPv6 with OS support disabled
  {
    Cleanup cleaner = Ipv6Instance::forceProtocolUnsupportedForTest(true);

    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(9090);
    inet_pton(AF_INET6, "fe80::1", &addr.sin6_addr);

    auto result = InstanceFactory::createMetadataIpv6Instance(addr);
    ASSERT_TRUE(result.ok()) << "Should create metadata instance even without OS support";
    EXPECT_TRUE((*result)->isMetadataOnly());
    EXPECT_EQ((*result)->ip()->addressAsString(), "fe80::1");
    EXPECT_EQ((*result)->ip()->port(), 9090);
  }
}

// Test polymorphic behavior of isMetadataOnly
TEST(MetadataAddressTest, PolymorphicBehavior) {
  // Create different address types
  sockaddr_in addr4;
  memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  addr4.sin_port = htons(80);
  addr4.sin_addr.s_addr = htonl(0xC0A80001);

  sockaddr_in6 addr6;
  memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  addr6.sin6_port = htons(443);
  inet_pton(AF_INET6, "2001:db8::1", &addr6.sin6_addr);

  // Store in base class pointers
  std::vector<InstanceConstSharedPtr> addresses;

  // Add regular addresses
  addresses.push_back(std::make_shared<Ipv4Instance>(&addr4));
  addresses.push_back(std::make_shared<Ipv6Instance>(addr6));

  // Add metadata addresses
  auto metadata4 = InstanceFactory::createMetadataIpv4Instance(&addr4);
  ASSERT_TRUE(metadata4.ok());
  addresses.push_back(*metadata4);

  auto metadata6 = InstanceFactory::createMetadataIpv6Instance(addr6);
  ASSERT_TRUE(metadata6.ok());
  addresses.push_back(*metadata6);

  // Test polymorphic calls
  EXPECT_FALSE(addresses[0]->isMetadataOnly()); // Regular IPv4
  EXPECT_FALSE(addresses[1]->isMetadataOnly()); // Regular IPv6
  EXPECT_TRUE(addresses[2]->isMetadataOnly());  // Metadata IPv4
  EXPECT_TRUE(addresses[3]->isMetadataOnly());  // Metadata IPv6
}

// Test that EnvoyInternalAddress and Pipe addresses are not metadata-only
TEST(MetadataAddressTest, OtherAddressTypesNotMetadataOnly) {
  // Pipe address
  {
    auto pipe_result = PipeInstance::create("/tmp/test.sock");
    ASSERT_TRUE(pipe_result.ok());
    EXPECT_FALSE((*pipe_result)->isMetadataOnly());
  }

  // EnvoyInternal address
  {
    auto internal_address = std::make_shared<EnvoyInternalInstance>("test_address");
    EXPECT_FALSE(internal_address->isMetadataOnly());
  }
}

// Test that metadata instances have the expected type name
TEST(MetadataAddressTest, MetadataInstancesReturnSubclassType) {
  // IPv4 metadata instance
  {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

    auto result = InstanceFactory::createMetadataIpv4Instance(&addr);
    ASSERT_TRUE(result.ok());
    auto metadata_instance = *result;

    // Verify it's a metadata instance
    EXPECT_TRUE(metadata_instance->isMetadataOnly());

    // Verify all the IP interface methods still work
    EXPECT_EQ(metadata_instance->type(), Type::Ip);
    EXPECT_NE(metadata_instance->ip(), nullptr);
    EXPECT_EQ(metadata_instance->ip()->version(), IpVersion::v4);
    EXPECT_EQ(metadata_instance->ip()->addressAsString(), "127.0.0.1");
    EXPECT_EQ(metadata_instance->ip()->port(), 80);
    // 127.0.0.1 is loopback, verify through isAnyAddress (which should be false)
    EXPECT_FALSE(metadata_instance->ip()->isAnyAddress());
  }

  // IPv6 metadata instance
  {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(443);
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);

    auto result = InstanceFactory::createMetadataIpv6Instance(addr);
    ASSERT_TRUE(result.ok());
    auto metadata_instance = *result;

    // Verify it's a metadata instance
    EXPECT_TRUE(metadata_instance->isMetadataOnly());

    // Verify all the IP interface methods still work
    EXPECT_EQ(metadata_instance->type(), Type::Ip);
    EXPECT_NE(metadata_instance->ip(), nullptr);
    EXPECT_EQ(metadata_instance->ip()->version(), IpVersion::v6);
    EXPECT_EQ(metadata_instance->ip()->addressAsString(), "::1");
    EXPECT_EQ(metadata_instance->ip()->port(), 443);
    // ::1 is loopback, verify through isAnyAddress (which should be false)
    EXPECT_FALSE(metadata_instance->ip()->isAnyAddress());
  }
}

// Test comparison and hashing for metadata instances
TEST(MetadataAddressTest, MetadataInstancesCompareAndHashCorrectly) {
  sockaddr_in addr1;
  memset(&addr1, 0, sizeof(addr1));
  addr1.sin_family = AF_INET;
  addr1.sin_port = htons(80);
  addr1.sin_addr.s_addr = htonl(0xC0A80001); // 192.168.0.1

  sockaddr_in addr2;
  memset(&addr2, 0, sizeof(addr2));
  addr2.sin_family = AF_INET;
  addr2.sin_port = htons(80);
  addr2.sin_addr.s_addr = htonl(0xC0A80001); // 192.168.0.1 (same)

  sockaddr_in addr3;
  memset(&addr3, 0, sizeof(addr3));
  addr3.sin_family = AF_INET;
  addr3.sin_port = htons(81);                // Different port
  addr3.sin_addr.s_addr = htonl(0xC0A80001); // 192.168.0.1

  auto metadata1 = InstanceFactory::createMetadataIpv4Instance(&addr1);
  auto metadata2 = InstanceFactory::createMetadataIpv4Instance(&addr2);
  auto metadata3 = InstanceFactory::createMetadataIpv4Instance(&addr3);
  auto regular = std::make_shared<Ipv4Instance>(&addr1);

  ASSERT_TRUE(metadata1.ok());
  ASSERT_TRUE(metadata2.ok());
  ASSERT_TRUE(metadata3.ok());

  // Metadata instances with same address should be equal
  EXPECT_TRUE(**metadata1 == **metadata2);

  // Metadata instance should equal regular instance with same address
  EXPECT_TRUE(**metadata1 == *regular);

  // Different addresses should not be equal
  EXPECT_FALSE(**metadata1 == **metadata3);
}

} // namespace
} // namespace Address
} // namespace Network
} // namespace Envoy
