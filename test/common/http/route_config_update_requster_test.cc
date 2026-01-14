#include "source/common/http/route_config_update_requster.h"

#include "source/common/runtime/runtime_features.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {
namespace {

class RouteConfigUpdateRequesterTest : public testing::Test {
public:
  RouteConfigUpdateRequesterTest() {}

  NiceMock<Router::MockRouteConfigProvider> route_config_provider_;
  NiceMock<Event::MockDispatcher> dispatcher_;
};

// Test that host header is lowercased by default (case-insensitive matching)
TEST_F(RouteConfigUpdateRequesterTest, VhdsCaseInsensitiveMatchingDefault) {
  // Enable case-insensitive matching via runtime flag (default is true)
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.vhds_case_insensitive_match", true);

  RdsRouteConfigUpdateRequester requester(&route_config_provider_);

  // Create a mock route config that uses VHDS
  auto route_config = std::make_shared<NiceMock<Router::MockConfig>>();
  EXPECT_CALL(*route_config, usesVhds()).WillRepeatedly(Return(true));
  // Mock should return the runtime flag value
  EXPECT_CALL(*route_config, vhdsCaseInsensitiveMatch()).WillRepeatedly(testing::Invoke([]() {
    return Runtime::runtimeFeatureEnabled("envoy.reloadable_features.vhds_case_insensitive_match");
  }));

  // Setup request headers with mixed case host
  TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/test"},
      {":authority", "Example.Com"},
  };

  auto route_config_updated_cb = std::make_shared<RouteConfigUpdatedCallback>([](bool) {});

  // Expect that the host header is converted to lowercase
  EXPECT_CALL(route_config_provider_, requestVirtualHostsUpdate("example.com", _, _));

  NiceMock<Http::MockRouteCache> route_cache;
  requester.requestRouteConfigUpdate(route_cache, route_config_updated_cb, route_config,
                                     dispatcher_, headers);
}

// Test that host header is NOT lowercased when runtime flag is disabled
TEST_F(RouteConfigUpdateRequesterTest, VhdsCaseSensitiveMatching) {
  // Disable case-insensitive matching via runtime flag
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.vhds_case_insensitive_match", false);

  RdsRouteConfigUpdateRequester requester(&route_config_provider_);

  // Create a mock route config that uses VHDS
  auto route_config = std::make_shared<NiceMock<Router::MockConfig>>();
  EXPECT_CALL(*route_config, usesVhds()).WillRepeatedly(Return(true));
  // Mock should return the runtime flag value
  EXPECT_CALL(*route_config, vhdsCaseInsensitiveMatch()).WillRepeatedly(testing::Invoke([]() {
    return Runtime::runtimeFeatureEnabled("envoy.reloadable_features.vhds_case_insensitive_match");
  }));

  // Setup request headers with mixed case host
  TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/test"},
      {":authority", "Example.Com"},
  };

  auto route_config_updated_cb = std::make_shared<RouteConfigUpdatedCallback>([](bool) {});

  // Expect that the host header is kept as-is (NOT lowercased)
  EXPECT_CALL(route_config_provider_, requestVirtualHostsUpdate("Example.Com", _, _));

  NiceMock<Http::MockRouteCache> route_cache;
  requester.requestRouteConfigUpdate(route_cache, route_config_updated_cb, route_config,
                                     dispatcher_, headers);
}

// Test that uppercase host header is lowercased by default
TEST_F(RouteConfigUpdateRequesterTest, VhdsCaseInsensitiveMatchingUppercase) {
  // Enable case-insensitive matching via runtime flag (default is true)
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.vhds_case_insensitive_match", true);

  RdsRouteConfigUpdateRequester requester(&route_config_provider_);

  auto route_config = std::make_shared<NiceMock<Router::MockConfig>>();
  EXPECT_CALL(*route_config, usesVhds()).WillRepeatedly(Return(true));
  // Mock should return the runtime flag value
  EXPECT_CALL(*route_config, vhdsCaseInsensitiveMatch()).WillRepeatedly(testing::Invoke([]() {
    return Runtime::runtimeFeatureEnabled("envoy.reloadable_features.vhds_case_insensitive_match");
  }));

  TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/test"},
      {":authority", "EXAMPLE.COM"},
  };

  auto route_config_updated_cb = std::make_shared<RouteConfigUpdatedCallback>([](bool) {});

  EXPECT_CALL(route_config_provider_, requestVirtualHostsUpdate("example.com", _, _));

  NiceMock<Http::MockRouteCache> route_cache;
  requester.requestRouteConfigUpdate(route_cache, route_config_updated_cb, route_config,
                                     dispatcher_, headers);
}

// Test that uppercase host header is preserved when runtime flag is disabled
TEST_F(RouteConfigUpdateRequesterTest, VhdsCaseSensitiveMatchingUppercase) {
  // Disable case-insensitive matching via runtime flag
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.vhds_case_insensitive_match", false);

  RdsRouteConfigUpdateRequester requester(&route_config_provider_);

  auto route_config = std::make_shared<NiceMock<Router::MockConfig>>();
  EXPECT_CALL(*route_config, usesVhds()).WillRepeatedly(Return(true));
  // Mock should return the runtime flag value
  EXPECT_CALL(*route_config, vhdsCaseInsensitiveMatch()).WillRepeatedly(testing::Invoke([]() {
    return Runtime::runtimeFeatureEnabled("envoy.reloadable_features.vhds_case_insensitive_match");
  }));

  TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/test"},
      {":authority", "EXAMPLE.COM"},
  };

  auto route_config_updated_cb = std::make_shared<RouteConfigUpdatedCallback>([](bool) {});

  EXPECT_CALL(route_config_provider_, requestVirtualHostsUpdate("EXAMPLE.COM", _, _));

  NiceMock<Http::MockRouteCache> route_cache;
  requester.requestRouteConfigUpdate(route_cache, route_config_updated_cb, route_config,
                                     dispatcher_, headers);
}

// Test that lowercase host header stays lowercase by default
TEST_F(RouteConfigUpdateRequesterTest, VhdsCaseInsensitiveMatchingLowercase) {
  // Enable case-insensitive matching via runtime flag (default is true)
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.vhds_case_insensitive_match", true);

  RdsRouteConfigUpdateRequester requester(&route_config_provider_);

  auto route_config = std::make_shared<NiceMock<Router::MockConfig>>();
  EXPECT_CALL(*route_config, usesVhds()).WillRepeatedly(Return(true));
  // Mock should return the runtime flag value
  EXPECT_CALL(*route_config, vhdsCaseInsensitiveMatch()).WillRepeatedly(testing::Invoke([]() {
    return Runtime::runtimeFeatureEnabled("envoy.reloadable_features.vhds_case_insensitive_match");
  }));

  TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/test"},
      {":authority", "example.com"},
  };

  auto route_config_updated_cb = std::make_shared<RouteConfigUpdatedCallback>([](bool) {});

  EXPECT_CALL(route_config_provider_, requestVirtualHostsUpdate("example.com", _, _));

  NiceMock<Http::MockRouteCache> route_cache;
  requester.requestRouteConfigUpdate(route_cache, route_config_updated_cb, route_config,
                                     dispatcher_, headers);
}

} // namespace
} // namespace Http
} // namespace Envoy
