#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.h"

#include "source/common/network/socket_option_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/http/original_src/original_src.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::SaveArg;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {
namespace {

class OriginalSrcHttpTest : public testing::Test {
public:
  std::unique_ptr<OriginalSrcFilter> makeDefaultFilter() {
    return makeFilterWithCallbacks(callbacks_);
  }

  std::unique_ptr<OriginalSrcFilter>
  makeFilterWithCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
    const Config default_config;
    auto filter = std::make_unique<OriginalSrcFilter>(default_config);
    filter->setDecoderFilterCallbacks(callbacks);
    return filter;
  }

  std::unique_ptr<OriginalSrcFilter> makeMarkingFilter(uint32_t mark) {
    envoy::extensions::filters::http::original_src::v3::OriginalSrc proto_config;
    proto_config.set_mark(mark);

    const Config config(proto_config);
    auto filter = std::make_unique<OriginalSrcFilter>(config);
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

  void setAddressToReturn(const std::string& address) {
    callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
        *Network::Utility::resolveUrl(address));
  }

protected:
  StrictMock<MockBuffer> buffer_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnectionSocket> socket_;
  Http::TestRequestHeaderMapImpl headers_;
  Http::TestRequestTrailerMapImpl trailers_;

  absl::optional<Network::Socket::Option::Details>
  findOptionDetails(const Network::Socket::Options& options, Network::SocketOptionName name,
                    envoy::config::core::v3::SocketOption::SocketState state) {
    for (const auto& option : options) {
      const auto details = option->getOptionDetails(socket_, state);
      if (details.has_value() && details->name_ == name) {
        return details;
      }
    }

    return absl::nullopt;
  }
};

TEST_F(OriginalSrcHttpTest, OnNonIpAddressDecodeSkips) {
  auto filter = makeDefaultFilter();
  setAddressToReturn("unix://domain.socket");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).Times(0);
  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
}

TEST_F(OriginalSrcHttpTest, DecodeHeadersIpv4AddressAddsOption) {
  auto filter = makeDefaultFilter();

  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:0");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);

  NiceMock<Network::MockConnectionSocket> socket;
  for (const auto& option : *options) {
    option->setOption(socket, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  }
  EXPECT_EQ(*socket.connectionInfoProvider().localAddress(),
            *callbacks_.stream_info_.downstream_connection_info_provider_->remoteAddress());
}

TEST_F(OriginalSrcHttpTest, DecodeHeadersIpv4AddressUsesCorrectAddress) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:0");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);
  std::vector<uint8_t> key;
  for (const auto& option : *options) {
    option->hashKey(key);
  }

  // The first part of the hash is the address. Then come the other options. On Windows there are
  // is only the single option. On other platforms there are more that get hashed.
  EXPECT_EQ(key[0], 1);
  EXPECT_EQ(key[1], 2);
  EXPECT_EQ(key[2], 3);
  EXPECT_EQ(key[3], 4);
#ifndef WIN32
  EXPECT_GT(key.size(), 4);
#else
  EXPECT_EQ(key.size(), 4);
#endif
}

TEST_F(OriginalSrcHttpTest, DecodeHeadersIpv4AddressBleachesPort) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  NiceMock<Network::MockConnectionSocket> socket;
  const auto expected_address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4");

  for (const auto& option : *options) {
    option->setOption(socket, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  }
  EXPECT_EQ(*socket.connectionInfoProvider().localAddress(), *expected_address);
}

TEST_F(OriginalSrcHttpTest, FilterAddsTransparentOption) {
  if (!ENVOY_SOCKET_IP_TRANSPARENT.hasValue()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  const auto transparent_option = findOptionDetails(
      *options, ENVOY_SOCKET_IP_TRANSPARENT, envoy::config::core::v3::SocketOption::STATE_PREBIND);

  EXPECT_TRUE(transparent_option.has_value());
}

TEST_F(OriginalSrcHttpTest, FilterAddsMarkOption) {
  if (!ENVOY_SOCKET_SO_MARK.hasValue()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeMarkingFilter(1234);
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  const auto mark_option = findOptionDetails(*options, ENVOY_SOCKET_SO_MARK,
                                             envoy::config::core::v3::SocketOption::STATE_PREBIND);

  ASSERT_TRUE(mark_option.has_value());
  uint32_t value = 1234;
  absl::string_view value_as_bstr(reinterpret_cast<const char*>(&value), sizeof(value));
  EXPECT_EQ(value_as_bstr, mark_option->value_);
}

TEST_F(OriginalSrcHttpTest, Mark0NotAdded) {
  if (!ENVOY_SOCKET_SO_MARK.hasValue()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeMarkingFilter(0);
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  const auto mark_option = findOptionDetails(*options, ENVOY_SOCKET_SO_MARK,
                                             envoy::config::core::v3::SocketOption::STATE_PREBIND);

  ASSERT_FALSE(mark_option.has_value());
}

TEST_F(OriginalSrcHttpTest, TrailersAndDataEndStreamDoNothing) {
  // Use a strict mock to show that decodeData and decodeTrailers do nothing to the callback.
  StrictMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  auto filter = makeFilterWithCallbacks(callbacks);

  // This will be invoked in decodeHeaders.
  EXPECT_CALL(callbacks, addUpstreamSocketOptions(_));
  EXPECT_CALL(callbacks, streamInfo());
  callbacks.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4"));
  filter->decodeHeaders(headers_, true);

  // No new expectations => no side effects from calling these.
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(buffer_, true));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->decodeTrailers(trailers_));
}

TEST_F(OriginalSrcHttpTest, TrailersAndDataNotEndStreamDoNothing) {
  // Use a strict mock to show that decodeData and decodeTrailers do nothing to the callback.
  StrictMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  auto filter = makeFilterWithCallbacks(callbacks);

  // This will be invoked in decodeHeaders.
  EXPECT_CALL(callbacks, addUpstreamSocketOptions(_));
  EXPECT_CALL(callbacks, streamInfo());
  callbacks.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4"));
  filter->decodeHeaders(headers_, false);

  // No new expectations => no side effects from calling these.
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(buffer_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->decodeTrailers(trailers_));
}
} // namespace
} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
