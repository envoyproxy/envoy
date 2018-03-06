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

std::chrono::nanoseconds checkDuration(std::chrono::nanoseconds last,
                                       Optional<std::chrono::nanoseconds> timing) {
  EXPECT_TRUE(timing.valid());
  EXPECT_LE(last, timing.value());
  return timing.value();
}

TEST(RequestInfoImplTest, TimingTest) {
  MonotonicTime pre_start = std::chrono::steady_clock::now();
  RequestInfoImpl info(Http::Protocol::Http2);
  MonotonicTime post_start = std::chrono::steady_clock::now();

  const MonotonicTime& start = info.startTimeMonotonic();

  EXPECT_LE(pre_start, start) << "Start time was lower than expected";
  EXPECT_GE(post_start, start) << "Start time was higher than expected";

  EXPECT_FALSE(info.lastDownstreamRxByteReceived().valid());
  info.onLastDownstreamRxByteReceived();
  std::chrono::nanoseconds dur =
      checkDuration(std::chrono::nanoseconds{0}, info.lastDownstreamRxByteReceived());

  EXPECT_FALSE(info.firstUpstreamTxByteSent().valid());
  info.onFirstUpstreamTxByteSent();
  dur = checkDuration(dur, info.firstUpstreamTxByteSent());

  EXPECT_FALSE(info.lastUpstreamTxByteSent().valid());
  info.onLastUpstreamTxByteSent();
  dur = checkDuration(dur, info.lastUpstreamTxByteSent());

  EXPECT_FALSE(info.firstUpstreamRxByteReceived().valid());
  info.onFirstUpstreamRxByteReceived();
  dur = checkDuration(dur, info.firstUpstreamRxByteReceived());

  EXPECT_FALSE(info.lastUpstreamRxByteReceived().valid());
  info.onLastUpstreamRxByteReceived();
  dur = checkDuration(dur, info.lastUpstreamRxByteReceived());

  EXPECT_FALSE(info.firstDownstreamTxByteSent().valid());
  info.onFirstDownstreamTxByteSent();
  dur = checkDuration(dur, info.firstDownstreamTxByteSent());

  EXPECT_FALSE(info.lastDownstreamTxByteSent().valid());
  info.onLastDownstreamTxByteSent();
  dur = checkDuration(dur, info.lastDownstreamTxByteSent());

  EXPECT_FALSE(info.requestComplete().valid());
  info.onRequestComplete();
  dur = checkDuration(dur, info.requestComplete());
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
