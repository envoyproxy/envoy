#include "common/network/utility.h"

#include "extensions/filters/listener/original_src/original_src.h"
#include "extensions/filters/listener/original_src/original_src_socket_option.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
  std::vector<uint8_t> expected_key = {OriginalSrcSocketOption::IPV4_KEY};
  // add the padding
  expected_key.insert(expected_key.end(), 12, 0);
  expected_key.insert(expected_key.end(), {1, 2, 3, 4});

  EXPECT_EQ(key, expected_key);
}

} // namespace
} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
