#include "common/network/utility.h"

#include "extensions/filters/listener/original_src/original_src.h"
#include "extensions/filters/listener/original_src/original_src_socket_option.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Exactly;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {
namespace {

class OriginalSrcTest : public testing::Test {
public:
  std::unique_ptr<OriginalSrcFilter> makeDefaultFilter() {
    Config default_config;
    return std::make_unique<OriginalSrcFilter>(default_config);
  }

  void setAddressToReturn(const std::string& address) {
    callbacks_.socket_.remote_address_ = Network::Utility::resolveUrl(address);
  }

protected:
  MockBuffer buffer_;
  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
};

TEST_F(OriginalSrcTest, onNewConnectionUnixSocketSkips) {
  auto filter = makeDefaultFilter();
  setAddressToReturn("unix://domain.socket");
  EXPECT_CALL(callbacks_.socket_, addOption_(_)).Times(0);
  EXPECT_EQ(filter->onAccept(callbacks_), Network::FilterStatus::Continue);
}

TEST_F(OriginalSrcTest, onNewConnectionIpv4AddressAddsOption) {
  auto filter = makeDefaultFilter();

  Network::Socket::OptionConstSharedPtr option;
  setAddressToReturn("tcp://1.2.3.4:0");
  // Network::Socket::OptionsSharedPtr options{std::make_shared<Network::Socket::Options>()};
  EXPECT_CALL(callbacks_.socket_, addOption_(_)).WillOnce(SaveArg<0>(&option));

  EXPECT_EQ(filter->onAccept(callbacks_), Network::FilterStatus::Continue);
  ASSERT_NE(option, nullptr);
  EXPECT_NE(dynamic_cast<const OriginalSrcSocketOption*>(option.get()), nullptr);
}

TEST_F(OriginalSrcTest, onNewConnectionIpv4AddressUsesCorrectAddress) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionConstSharedPtr option;
  setAddressToReturn("tcp://1.2.3.4:0");
  Network::Socket::OptionsSharedPtr options{std::make_shared<Network::Socket::Options>()};
  EXPECT_CALL(callbacks_.socket_, addOption_(_)).WillOnce(SaveArg<0>(&option));

  filter->onAccept(callbacks_);
  std::vector<uint8_t> key;
  option->hashKey(key);
  std::vector<uint8_t> expected_key = {1, 2, 3, 4};

  EXPECT_EQ(key, expected_key);
}

TEST_F(OriginalSrcTest, onNewConnectionIpv4AddressBleachesPort) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionConstSharedPtr option;
  setAddressToReturn("tcp://1.2.3.4:80");
  Network::Socket::OptionsSharedPtr options{std::make_shared<Network::Socket::Options>()};
  EXPECT_CALL(callbacks_.socket_, addOption_(_)).WillOnce(SaveArg<0>(&option));

  filter->onAccept(callbacks_);

  NiceMock<Network::MockConnectionSocket> socket;
  const auto expected_address = Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(socket, setLocalAddress(PointeesEq(expected_address)));

  option->setOption(socket, envoy::api::v2::core::SocketOption::STATE_PREBIND);
}

} // namespace
} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
