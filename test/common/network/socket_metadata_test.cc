#include <memory>

#include "envoy/network/address.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_interface_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class SocketMetadataTest : public testing::Test {
protected:
  SocketInterfaceImpl socket_interface_impl_;
  const SocketInterface* socket_interface_ = &socket_interface_impl_;
};

// Test that bind() rejects metadata-only addresses.
TEST_F(SocketMetadataTest, BindRejectsMetadataAddresses) {
  // Create a metadata-only IPv4 address.
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);                 // Let OS choose port
  addr.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

  auto metadata_result = Address::InstanceFactory::createMetadataIpv4Instance(&addr);
  ASSERT_TRUE(metadata_result.ok());
  auto metadata_address = *metadata_result;
  EXPECT_TRUE(metadata_address->isMetadataOnly());

  // Test IoSocketHandleImpl::bind rejection.
  {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(fd, -1);

    auto io_handle = std::make_unique<IoSocketHandleImpl>(fd, false, absl::optional<int>(AF_INET));
    Api::SysCallIntResult result = io_handle->bind(metadata_address);

    EXPECT_EQ(result.return_value_, -1);
    EXPECT_EQ(result.errno_, EINVAL);

    ::close(fd);
  }
}

// Test that connect() rejects metadata-only addresses.
TEST_F(SocketMetadataTest, ConnectRejectsMetadataAddresses) {
  // Create a metadata-only IPv4 address
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(80);
  addr.sin_addr.s_addr = htonl(0x08080808); // 8.8.8.8

  auto metadata_result = Address::InstanceFactory::createMetadataIpv4Instance(&addr);
  ASSERT_TRUE(metadata_result.ok());
  auto metadata_address = *metadata_result;
  EXPECT_TRUE(metadata_address->isMetadataOnly());

  // Test IoSocketHandleImpl::connect rejection.
  {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(fd, -1);

    auto io_handle = std::make_unique<IoSocketHandleImpl>(fd, false, absl::optional<int>(AF_INET));
    Api::SysCallIntResult result = io_handle->connect(metadata_address);

    EXPECT_EQ(result.return_value_, -1);
    EXPECT_EQ(result.errno_, EINVAL);

    ::close(fd);
  }
}

// Test that regular addresses still work with bind/connect.
TEST_F(SocketMetadataTest, RegularAddressesWorkForSocketOps) {
  // Create a regular IPv4 address
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);                 // Let OS choose port
  addr.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

  auto regular_address = std::make_shared<Address::Ipv4Instance>(&addr);
  EXPECT_FALSE(regular_address->isMetadataOnly());

  // Test that bind doesn't reject with EINVAL.
  {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(fd, -1);

    auto io_handle = std::make_unique<IoSocketHandleImpl>(fd, false, absl::optional<int>(AF_INET));
    Api::SysCallIntResult result = io_handle->bind(regular_address);

    // Bind might fail for other reasons (permission, address in use, etc.)
    // but should not fail with EINVAL for a regular address.
    if (result.return_value_ == -1) {
      EXPECT_NE(result.errno_, EINVAL) << "Regular address should not be rejected with EINVAL";
    } else {
      // If bind succeeded, that's even better.
      EXPECT_EQ(result.return_value_, 0);
    }

    ::close(fd);
  }
}

// Test metadata addresses with IPv6.
TEST_F(SocketMetadataTest, IPv6MetadataAddresses) {
  // Create a metadata-only IPv6 address.
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(443);
  inet_pton(AF_INET6, "2001:db8::1", &addr.sin6_addr);

  auto metadata_result = Address::InstanceFactory::createMetadataIpv6Instance(addr);
  ASSERT_TRUE(metadata_result.ok());
  auto metadata_address = *metadata_result;
  EXPECT_TRUE(metadata_address->isMetadataOnly());

  // Only test if IPv6 is supported on this system.
  int test_fd = ::socket(AF_INET6, SOCK_STREAM, 0);
  if (test_fd != -1) {
    ::close(test_fd);

    // Test IoSocketHandleImpl::bind rejection.
    {
      int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
      ASSERT_NE(fd, -1);

      auto io_handle =
          std::make_unique<IoSocketHandleImpl>(fd, false, absl::optional<int>(AF_INET6));
      Api::SysCallIntResult result = io_handle->bind(metadata_address);

      EXPECT_EQ(result.return_value_, -1);
      EXPECT_EQ(result.errno_, EINVAL);

      ::close(fd);
    }

    // Test IoSocketHandleImpl::connect rejection.
    {
      int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
      ASSERT_NE(fd, -1);

      auto io_handle =
          std::make_unique<IoSocketHandleImpl>(fd, false, absl::optional<int>(AF_INET6));
      Api::SysCallIntResult result = io_handle->connect(metadata_address);

      EXPECT_EQ(result.return_value_, -1);
      EXPECT_EQ(result.errno_, EINVAL);

      ::close(fd);
    }
  }
}

} // namespace
} // namespace Network
} // namespace Envoy
