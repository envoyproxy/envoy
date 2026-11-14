#include "source/common/http/route_config_update_requster.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace {

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class TestRouteCache : public RouteCache {
public:
  bool hasCachedRoute() const override { return false; }
  void refreshCachedRoute() override {}
};

class RdsRouteConfigUpdateRequesterTest : public testing::Test {
protected:
  RdsRouteConfigUpdateRequesterTest() : requester_(&provider_) {
    ON_CALL(*route_config_, usesVhds()).WillByDefault(Return(true));
  }

  void requestUpdate(RequestHeaderMap& headers, bool& updated) {
    auto callback =
        std::make_shared<RouteConfigUpdatedCallback>([&updated](bool value) { updated = value; });
    requester_.requestRouteConfigUpdate(
        route_cache_, callback, std::make_optional<Router::ConfigConstSharedPtr>(route_config_),
        dispatcher_, headers);
  }

  NiceMock<Router::MockRouteConfigProvider> provider_;
  RdsRouteConfigUpdateRequester requester_;
  std::shared_ptr<NiceMock<Router::MockConfig>> route_config_{
      std::make_shared<NiceMock<Router::MockConfig>>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  TestRouteCache route_cache_;
};

TEST_F(RdsRouteConfigUpdateRequesterTest, VhdsUpdateWithHost) {
  EXPECT_CALL(provider_, requestVirtualHostsUpdate("some.host", _, _));

  bool updated = false;
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "Some.Host"}};
  requestUpdate(headers, updated);
}

// A request whose :authority header was removed or emptied by a filter must not crash the VHDS
// on-demand update path; the filter chain continues without an update.
TEST_F(RdsRouteConfigUpdateRequesterTest, VhdsUpdateWithoutHostContinuesFilterChain) {
  EXPECT_CALL(provider_, requestVirtualHostsUpdate(_, _, _)).Times(0);

  bool updated = true;
  TestRequestHeaderMapImpl missing_host{{":method", "GET"}, {":path", "/"}};
  requestUpdate(missing_host, updated);
  EXPECT_FALSE(updated);

  updated = true;
  TestRequestHeaderMapImpl empty_host{{":method", "GET"}, {":path", "/"}, {":authority", ""}};
  requestUpdate(empty_host, updated);
  EXPECT_FALSE(updated);
}

TEST_F(RdsRouteConfigUpdateRequesterTest, VhdsUpdateSkippedWhenWarmingBlocksUnmatchedRequests) {
  ON_CALL(*route_config_, warmingBlocksUnmatchedRequests()).WillByDefault(Return(true));
  EXPECT_CALL(provider_, requestVirtualHostsUpdate(_, _, _)).Times(0);

  bool updated = true;
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "Some.Host"}};
  requestUpdate(headers, updated);
  EXPECT_FALSE(updated);
}

TEST_F(RdsRouteConfigUpdateRequesterTest, VhdsUpdateWithAlternateVhostHeader) {
  const Http::LowerCaseString vhost_header("x-vhost");
  ON_CALL(*route_config_, vhostHeader()).WillByDefault(ReturnRef(vhost_header));

  EXPECT_CALL(provider_, requestVirtualHostsUpdate("custom.host", _, _));
  bool updated = false;
  TestRequestHeaderMapImpl headers{{":method", "GET"},
                                   {":path", "/"},
                                   {":authority", "Ignored.Host"},
                                   {"x-vhost", "Custom.Host"}};
  requestUpdate(headers, updated);
  EXPECT_TRUE(testing::Mock::VerifyAndClearExpectations(&provider_));

  EXPECT_CALL(provider_, requestVirtualHostsUpdate(_, _, _)).Times(0);
  updated = true;
  TestRequestHeaderMapImpl missing_vhost_header{
      {":method", "GET"}, {":path", "/"}, {":authority", "Ignored.Host"}};
  requestUpdate(missing_vhost_header, updated);
  EXPECT_FALSE(updated);

  updated = true;
  TestRequestHeaderMapImpl empty_vhost_header{
      {":method", "GET"}, {":path", "/"}, {":authority", "Ignored.Host"}, {"x-vhost", ""}};
  requestUpdate(empty_vhost_header, updated);
  EXPECT_FALSE(updated);
}

} // namespace
} // namespace Http
} // namespace Envoy
