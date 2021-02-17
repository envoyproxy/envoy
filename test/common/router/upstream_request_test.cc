#include "common/common/utility.h"
#include "common/network/utility.h"
#include "common/router/upstream_request.h"

#include "test/common/http/common.h"
#include "test/mocks/router/router_filter_interface.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

class UpstreamRequestTest : public testing::Test {
public:
  NiceMock<MockRouterFilterInterface> router_filter_interface_;
  UpstreamRequest upstream_request_{router_filter_interface_,
                                    std::make_unique<NiceMock<Router::MockGenericConnPool>>()};
};

// UpstreamRequest is responsible processing for passing 101 upgrade headers to onUpstreamHeaders.
TEST_F(UpstreamRequestTest, Decode101UpgradeHeaders) {
  auto upgrade_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "101"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_.decodeHeaders(std::move(upgrade_headers), false);
}

// UpstreamRequest is responsible for ignoring non-{100,101} 1xx headers.
TEST_F(UpstreamRequestTest, IgnoreOther1xxHeaders) {
  auto other_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "102"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _)).Times(0);
  upstream_request_.decodeHeaders(std::move(other_headers), false);
}

// UpstreamRequest is responsible processing for passing 200 upgrade headers to onUpstreamHeaders.
TEST_F(UpstreamRequestTest, Decode200UpgradeHeaders) {
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_.decodeHeaders(std::move(response_headers), false);
}

// UpstreamRequest dumpState without allocating memory.
TEST_F(UpstreamRequestTest, DumpsStateWithoutAllocatingMemory) {
  // Set up router filter
  auto address_provider =
      router_filter_interface_.client_connection_.stream_info_.downstream_address_provider_;
  address_provider->setRemoteAddress(Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));
  address_provider->setLocalAddress(Network::Utility::parseInternetAddressAndPort("5.6.7.8:5678"));
  address_provider->setDirectRemoteAddressForTest(
      Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));
  Http::TestRequestHeaderMapImpl downstream_request_header_map;
  HttpTestUtility::addDefaultHeaders(downstream_request_header_map);

  EXPECT_CALL(router_filter_interface_, downstreamHeaders())
      .WillOnce(Return(&downstream_request_header_map));

  // Dump State
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  Stats::TestUtil::MemoryTest memory_test;
  upstream_request_.dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);

  // Check Contents
  EXPECT_THAT(ostream.contents(), HasSubstr("UpstreamRequest "));
  EXPECT_THAT(ostream.contents(), HasSubstr("addressProvider: \n  SocketAddressSetterImpl "));
  EXPECT_THAT(ostream.contents(), HasSubstr("request_headers: \n"));
}

} // namespace
} // namespace Router
} // namespace Envoy
