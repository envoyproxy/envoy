#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"

#include "source/common/network/utility.h"
#include "source/extensions/filters/common/original_src/original_src_socket_option.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace OriginalSrc {
namespace {

class OriginalSrcSocketOptionTest : public testing::Test {
public:
  std::unique_ptr<OriginalSrcSocketOption>
  makeOptionByAddress(const Network::Address::InstanceConstSharedPtr& address) {
    return std::make_unique<OriginalSrcSocketOption>(address);
  }

protected:
  NiceMock<Network::MockConnectionSocket> socket_;
  std::vector<uint8_t> key_;
};

TEST_F(OriginalSrcSocketOptionTest, TestSetOptionPreBindSetsAddress) {
  const auto address = Network::Utility::parseInternetAddress("127.0.0.2");
  auto option = makeOptionByAddress(address);
  EXPECT_EQ(option->setOption(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND), true);
  EXPECT_EQ(*socket_.connection_info_provider_->localAddress(), *address);
}

TEST_F(OriginalSrcSocketOptionTest, TestSetOptionPreBindSetsAddressSecond) {
  const auto address = Network::Utility::parseInternetAddress("1.2.3.4");
  auto option = makeOptionByAddress(address);
  EXPECT_EQ(option->setOption(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND), true);
  EXPECT_EQ(*socket_.connection_info_provider_->localAddress(), *address);
}

TEST_F(OriginalSrcSocketOptionTest, TestSetOptionNotPrebindDoesNotSetAddress) {
  const auto address = Network::Utility::parseInternetAddress("1.2.3.4");
  auto option = makeOptionByAddress(address);
  EXPECT_EQ(option->setOption(socket_, envoy::config::core::v3::SocketOption::STATE_LISTENING),
            true);
  EXPECT_NE(*socket_.connection_info_provider_->localAddress(), *address);
}

TEST_F(OriginalSrcSocketOptionTest, TestIpv4HashKey) {
  const auto address = Network::Utility::parseInternetAddress("1.2.3.4");
  auto option = makeOptionByAddress(address);
  option->hashKey(key_);

  // The ip address broken into big-endian octets.
  std::vector<uint8_t> expected_key = {1, 2, 3, 4};
  EXPECT_EQ(key_, expected_key);
}

TEST_F(OriginalSrcSocketOptionTest, TestIpv4HashKeyOther) {
  const auto address = Network::Utility::parseInternetAddress("255.254.253.0");
  auto option = makeOptionByAddress(address);
  option->hashKey(key_);

  // The ip address broken into big-endian octets.
  std::vector<uint8_t> expected_key = {255, 254, 253, 0};
  EXPECT_EQ(key_, expected_key);
}

TEST_F(OriginalSrcSocketOptionTest, TestIpv6HashKey) {
  const auto address = Network::Utility::parseInternetAddress("102:304:506:708:90a:b0c:d0e:f00");
  auto option = makeOptionByAddress(address);
  option->hashKey(key_);

  std::vector<uint8_t> expected_key = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,
                                       0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x0};
  EXPECT_EQ(key_, expected_key);
}

TEST_F(OriginalSrcSocketOptionTest, TestIpv6HashKeyOther) {
  const auto address = Network::Utility::parseInternetAddress("F02:304:519:708:90a:b0e:FFFF:0000");
  auto option = makeOptionByAddress(address);
  option->hashKey(key_);

  std::vector<uint8_t> expected_key = {0xF, 0x2, 0x3, 0x4, 0x5,  0x19, 0x7, 0x8,
                                       0x9, 0xa, 0xb, 0xe, 0xff, 0xff, 0x0, 0x0};
  EXPECT_EQ(key_, expected_key);
}

TEST_F(OriginalSrcSocketOptionTest, TestOptionDetailsNotSupported) {
  const auto address = Network::Utility::parseInternetAddress("255.254.253.0");
  auto option = makeOptionByAddress(address);

  auto details =
      option->getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);

  EXPECT_FALSE(details.has_value());
}

} // namespace
} // namespace OriginalSrc
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
