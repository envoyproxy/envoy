#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/listener/original_src/v3/original_src.pb.h"

#include "common/network/socket_option_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/listener/original_src/original_src.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
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

  std::unique_ptr<OriginalSrcFilter> makeMarkingFilter(uint32_t mark) {
    envoy::extensions::filters::listener::original_src::v3::OriginalSrc proto_config;
    proto_config.set_mark(mark);

    Config config(proto_config);
    return std::make_unique<OriginalSrcFilter>(config);
  }

  void setAddressToReturn(const std::string& address) {
    callbacks_.socket_.remote_address_ = Network::Utility::resolveUrl(address);
  }

protected:
  MockBuffer buffer_;
  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;

  absl::optional<Network::Socket::Option::Details>
  findOptionDetails(const Network::Socket::Options& options, Network::SocketOptionName name,
                    envoy::config::core::v3::SocketOption::SocketState state) {
    for (const auto& option : options) {
      auto details = option->getOptionDetails(callbacks_.socket_, state);
      if (details.has_value() && details->name_ == name) {
        return details;
      }
    }

    return absl::nullopt;
  }
};

TEST_F(OriginalSrcTest, OnNewConnectionUnixSocketSkips) {
  auto filter = makeDefaultFilter();
  setAddressToReturn("unix://domain.socket");
  EXPECT_CALL(callbacks_.socket_, addOption_(_)).Times(0);
  EXPECT_EQ(filter->onAccept(callbacks_), Network::FilterStatus::Continue);
}

TEST_F(OriginalSrcTest, OnNewConnectionIpv4AddressAddsOption) {
  auto filter = makeDefaultFilter();

  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:0");
  EXPECT_CALL(callbacks_.socket_, addOptions_(_)).WillOnce(SaveArg<0>(&options));

  EXPECT_EQ(filter->onAccept(callbacks_), Network::FilterStatus::Continue);

  // not ideal -- we're assuming that the original_src option is first, but it's a fair assumption
  // for now.
  ASSERT_NE(options->at(0), nullptr);

  NiceMock<Network::MockConnectionSocket> socket;
  EXPECT_CALL(socket, setLocalAddress(PointeesEq(callbacks_.socket_.remote_address_)));
  options->at(0)->setOption(socket, envoy::config::core::v3::SocketOption::STATE_PREBIND);
}

TEST_F(OriginalSrcTest, OnNewConnectionIpv4AddressUsesCorrectAddress) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:0");
  EXPECT_CALL(callbacks_.socket_, addOptions_(_)).WillOnce(SaveArg<0>(&options));

  filter->onAccept(callbacks_);
  std::vector<uint8_t> key;
  // not ideal -- we're assuming that the original_src option is first, but it's a fair assumption
  // for now.
  options->at(0)->hashKey(key);
  std::vector<uint8_t> expected_key = {1, 2, 3, 4};

  EXPECT_EQ(key, expected_key);
}

TEST_F(OriginalSrcTest, OnNewConnectionIpv4AddressBleachesPort) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_.socket_, addOptions_(_)).WillOnce(SaveArg<0>(&options));

  filter->onAccept(callbacks_);

  NiceMock<Network::MockConnectionSocket> socket;
  const auto expected_address = Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(socket, setLocalAddress(PointeesEq(expected_address)));

  // not ideal -- we're assuming that the original_src option is first, but it's a fair assumption
  // for now.
  options->at(0)->setOption(socket, envoy::config::core::v3::SocketOption::STATE_PREBIND);
}

TEST_F(OriginalSrcTest, FilterAddsTransparentOption) {
  if (!ENVOY_SOCKET_IP_TRANSPARENT.has_value()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_.socket_, addOptions_(_)).WillOnce(SaveArg<0>(&options));

  filter->onAccept(callbacks_);

  auto transparent_option = findOptionDetails(*options, ENVOY_SOCKET_IP_TRANSPARENT,
                                              envoy::config::core::v3::SocketOption::STATE_PREBIND);

  EXPECT_TRUE(transparent_option.has_value());
}

TEST_F(OriginalSrcTest, FilterAddsMarkOption) {
  if (!ENVOY_SOCKET_SO_MARK.has_value()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeMarkingFilter(1234);
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_.socket_, addOptions_(_)).WillOnce(SaveArg<0>(&options));

  filter->onAccept(callbacks_);

  auto mark_option = findOptionDetails(*options, ENVOY_SOCKET_SO_MARK,
                                       envoy::config::core::v3::SocketOption::STATE_PREBIND);

  ASSERT_TRUE(mark_option.has_value());
  uint32_t value = 1234;
  absl::string_view value_as_bstr(reinterpret_cast<const char*>(&value), sizeof(value));
  EXPECT_EQ(value_as_bstr, mark_option->value_);
}

TEST_F(OriginalSrcTest, Mark0NotAdded) {
  if (!ENVOY_SOCKET_SO_MARK.has_value()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeMarkingFilter(0);
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_.socket_, addOptions_(_)).WillOnce(SaveArg<0>(&options));

  filter->onAccept(callbacks_);

  auto mark_option = findOptionDetails(*options, ENVOY_SOCKET_SO_MARK,
                                       envoy::config::core::v3::SocketOption::STATE_PREBIND);

  ASSERT_FALSE(mark_option.has_value());
}

} // namespace
} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
