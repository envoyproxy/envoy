#include "common/router/upstream_request.h"

#include "test/mocks/router/router_filter_interface.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

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

} // namespace
} // namespace Router
} // namespace Envoy
