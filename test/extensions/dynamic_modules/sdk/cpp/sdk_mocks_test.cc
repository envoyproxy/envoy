#include "gtest/gtest.h"
#include "sdk_mocks.h"

namespace Envoy {
namespace DynamicModules {

// Compile-time checks that each mock correctly inherits from its base class.
static_assert(std::is_base_of_v<BodyBuffer, MockBodyBuffer>);
static_assert(std::is_base_of_v<HeaderMap, MockHeaderMap>);
static_assert(std::is_base_of_v<Scheduler, MockScheduler>);
static_assert(std::is_base_of_v<HttpCalloutCallback, MockHttpCalloutCallback>);
static_assert(std::is_base_of_v<HttpStreamCallback, MockHttpStreamCallback>);
static_assert(std::is_base_of_v<DownstreamWatermarkCallbacks, MockDownstreamWatermarkCallbacks>);
static_assert(std::is_base_of_v<HttpFilterConfigHandle, MockHttpFilterConfigHandle>);
static_assert(std::is_base_of_v<HttpFilterHandle, MockHttpFilterHandle>);
static_assert(std::is_base_of_v<HttpFilter, MockHttpFilter>);

// Instantiation tests: each mock must be concrete (all pure virtual methods overridden).
// If any pure virtual is missing from the mock, this file will fail to compile.
TEST(SdkMocksTest, MocksAreInstantiable) {
  MockBodyBuffer body_buffer;
  MockHeaderMap header_map;
  MockScheduler scheduler;
  MockHttpCalloutCallback callout_cb;
  MockHttpStreamCallback stream_cb;
  MockDownstreamWatermarkCallbacks watermark_cb;
  MockHttpFilterConfigHandle config_handle;
  MockHttpFilterHandle filter_handle;
  MockHttpFilter filter;
}

TEST(SdkMocksTest, RefreshRouteClusterIsMockable) {
  MockHttpFilterHandle handle;
  EXPECT_CALL(handle, refreshRouteCluster()).Times(1);
  handle.refreshRouteCluster();
}

} // namespace DynamicModules
} // namespace Envoy
