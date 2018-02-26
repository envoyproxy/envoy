#include <chrono>
#include <functional>

#include "envoy/http/protocol.h"
#include "envoy/upstream/host_description.h"

#include "common/common/fmt.h"
#include "common/request_info/request_info_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace RequestInfo {
namespace {

TEST(RequestInfoImplTest, TimingTest) {
  MonotonicTime pre_start = std::chrono::steady_clock::now();
  RequestInfoImpl info(Http::Protocol::Http2);
  MonotonicTime post_start = std::chrono::steady_clock::now();

  const MonotonicTime& start = info.startTimeMonotonic();

  EXPECT_LE(pre_start, start) << "Start time was lower than expected";
  EXPECT_GE(post_start, start) << "Start time was higher than expected";

  std::chrono::nanoseconds dur = std::chrono::milliseconds(1);
  EXPECT_FALSE(info.lastDownstreamRxByteReceived().valid());
  info.lastDownstreamRxByteReceived(start + dur);
  Optional<std::chrono::nanoseconds> timing = info.lastDownstreamRxByteReceived();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());

  dur += std::chrono::milliseconds(1);
  EXPECT_FALSE(info.firstUpstreamTxByteSent().valid());
  info.firstUpstreamTxByteSent(start + dur);
  timing = info.firstUpstreamTxByteSent();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());

  dur += std::chrono::milliseconds(1);
  EXPECT_FALSE(info.lastUpstreamTxByteSent().valid());
  info.lastUpstreamTxByteSent(start + dur);
  timing = info.lastUpstreamTxByteSent();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());

  dur += std::chrono::milliseconds(1);
  EXPECT_FALSE(info.firstUpstreamRxByteReceived().valid());
  info.firstUpstreamRxByteReceived(start + dur);
  timing = info.firstUpstreamRxByteReceived();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());

  dur += std::chrono::milliseconds(1);
  EXPECT_FALSE(info.lastUpstreamRxByteReceived().valid());
  info.lastUpstreamRxByteReceived(start + dur);
  timing = info.lastUpstreamRxByteReceived();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());

  dur += std::chrono::milliseconds(1);
  EXPECT_FALSE(info.firstDownstreamTxByteSent().valid());
  info.firstDownstreamTxByteSent(start + dur);
  timing = info.firstDownstreamTxByteSent();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());

  dur += std::chrono::milliseconds(1);
  EXPECT_FALSE(info.lastDownstreamTxByteSent().valid());
  info.lastDownstreamTxByteSent(start + dur);
  timing = info.lastDownstreamTxByteSent();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());

  dur += std::chrono::milliseconds(1);
  EXPECT_FALSE(info.finalTimeMonotonic().valid());
  info.finalize(start + dur);
  timing = info.finalTimeMonotonic();
  EXPECT_TRUE(timing.valid());
  EXPECT_EQ(dur, timing.value());
}

TEST(RequestInfoImplTest, BytesTest) {
  RequestInfoImpl request_info(Http::Protocol::Http2);
  const uint64_t bytes_sent = 7;
  const uint64_t bytes_received = 12;

  request_info.bytes_sent_ = bytes_sent;
  request_info.bytes_received_ = bytes_received;

  EXPECT_EQ(bytes_sent, request_info.bytesSent());
  EXPECT_EQ(bytes_received, request_info.bytesReceived());
}

TEST(RequestInfoImplTest, ResponseFlagTest) {
  const std::vector<ResponseFlag> responseFlags = {FailedLocalHealthCheck,
                                                   NoHealthyUpstream,
                                                   UpstreamRequestTimeout,
                                                   LocalReset,
                                                   UpstreamRemoteReset,
                                                   UpstreamConnectionFailure,
                                                   UpstreamConnectionTermination,
                                                   UpstreamOverflow,
                                                   NoRouteFound,
                                                   DelayInjected,
                                                   FaultInjected,
                                                   RateLimited};

  RequestInfoImpl request_info(Http::Protocol::Http2);
  for (ResponseFlag flag : responseFlags) {
    // Test cumulative setting of response flags.
    EXPECT_FALSE(request_info.getResponseFlag(flag))
        << fmt::format("Flag: {} was already set", flag);
    request_info.setResponseFlag(flag);
    EXPECT_TRUE(request_info.getResponseFlag(flag))
        << fmt::format("Flag: {} was expected to be set", flag);
  }
}

TEST(RequestInfoImplTest, MiscSettersAndGetters) {
  {
    RequestInfoImpl request_info(Http::Protocol::Http2);
    EXPECT_EQ(Http::Protocol::Http2, request_info.protocol().value());

    request_info.protocol(Http::Protocol::Http10);
    EXPECT_EQ(Http::Protocol::Http10, request_info.protocol().value());

    EXPECT_FALSE(request_info.responseCode().valid());
    request_info.response_code_ = 200;
    ASSERT_TRUE(request_info.responseCode().valid());
    EXPECT_EQ(200, request_info.responseCode().value());

    EXPECT_EQ(nullptr, request_info.upstreamHost());
    Upstream::HostDescriptionConstSharedPtr host(new NiceMock<Upstream::MockHostDescription>());
    request_info.onUpstreamHostSelected(host);
    EXPECT_EQ(host, request_info.upstreamHost());

    EXPECT_FALSE(request_info.healthCheck());
    request_info.healthCheck(true);
    EXPECT_TRUE(request_info.healthCheck());

    EXPECT_EQ(nullptr, request_info.routeEntry());
    NiceMock<Router::MockRouteEntry> route_entry;
    request_info.route_entry_ = &route_entry;
    EXPECT_EQ(&route_entry, request_info.routeEntry());
  }
}

} // namespace
} // namespace RequestInfo
} // namespace Envoy
