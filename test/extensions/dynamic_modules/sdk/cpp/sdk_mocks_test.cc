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

// Verifies that `DYM_LOG` forwards the caller source location to the handle log method.
TEST(SdkMocksTest, DymLogForwardsCallerSourceLocation) {
  testing::StrictMock<MockHttpFilterHandle> handle;
  EXPECT_CALL(handle, logEnabled(LogLevel::Info)).WillOnce(testing::Return(true));
  std::source_location captured{};
  EXPECT_CALL(handle, log(LogLevel::Info, std::string_view("hello 1"), testing::_))
      .WillOnce([&captured](LogLevel, std::string_view, std::source_location location) {
        captured = location;
      });
  const int expected_line = __LINE__ + 1;
  DYM_LOG(handle, LogLevel::Info, "hello {}", 1);
  EXPECT_EQ(expected_line, static_cast<int>(captured.line()));
  EXPECT_NE(std::string_view::npos,
            std::string_view(captured.file_name()).find("sdk_mocks_test.cc"));
}

// Verifies that `DYM_LOG` skips the log call when the level is disabled.
TEST(SdkMocksTest, DymLogSkipsWhenLevelDisabled) {
  testing::StrictMock<MockHttpFilterHandle> handle;
  EXPECT_CALL(handle, logEnabled(LogLevel::Trace)).WillOnce(testing::Return(false));
  DYM_LOG(handle, LogLevel::Trace, "should not log");
}

} // namespace DynamicModules
} // namespace Envoy
