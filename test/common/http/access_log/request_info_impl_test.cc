#include <chrono>
#include <functional>

#include "envoy/http/protocol.h"
#include "envoy/upstream/host_description.h"

#include "common/http/access_log/request_info_impl.h"

#include "test/mocks/upstream/mocks.h"

#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace AccessLog {
namespace {

class RequestInfoTimingWrapper {
public:
  RequestInfoTimingWrapper()
      : pre_start_(std::chrono::steady_clock::now()), request_info_(Protocol::Http2),
        post_start_(std::chrono::steady_clock::now()) {}

  void checkTimingBounds(
      const std::function<std::chrono::microseconds(RequestInfoImpl&)>& measure_duration,
      const std::string& duration_name) {
    MonotonicTime pre_measurement = std::chrono::steady_clock::now();
    std::chrono::microseconds duration = measure_duration(request_info_);
    MonotonicTime post_measurement = std::chrono::steady_clock::now();

    std::chrono::microseconds lower_bound =
        std::chrono::duration_cast<std::chrono::microseconds>(pre_measurement - post_start_);
    EXPECT_LE(lower_bound, duration)
        << fmt::format("Duration {} was lower than expected", duration_name);

    std::chrono::microseconds upper_bound =
        std::chrono::duration_cast<std::chrono::microseconds>(post_measurement - pre_start_);
    EXPECT_GE(upper_bound, duration)
        << fmt::format("Duration: {} was higher than expected", duration_name);
  }

private:
  const MonotonicTime pre_start_;
  RequestInfoImpl request_info_;
  const MonotonicTime post_start_;
};

TEST(RequestInfoImplTest, TimingTest) {
  RequestInfoTimingWrapper wrapper;

  wrapper.checkTimingBounds(
      [](RequestInfoImpl& request_info) {
        request_info.requestReceivedDuration(std::chrono::steady_clock::now());
        return request_info.requestReceivedDuration();
      },
      "request received");

  wrapper.checkTimingBounds(
      [](RequestInfoImpl& request_info) {
        request_info.responseReceivedDuration(std::chrono::steady_clock::now());
        return request_info.responseReceivedDuration();
      },
      "response received");

  wrapper.checkTimingBounds([](RequestInfoImpl& request_info) { return request_info.duration(); },
                            "stream duration");
}

TEST(RequestInfoImplTest, BytesTest) {
  RequestInfoImpl request_info(Protocol::Http2);
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

  RequestInfoImpl request_info(Protocol::Http2);
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
  RequestInfoImpl request_info(Protocol::Http2);
  EXPECT_EQ(Protocol::Http2, request_info.protocol());

  request_info.protocol(Protocol::Http10);
  EXPECT_EQ(Protocol::Http10, request_info.protocol());

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
}

} // namespace
} // namespace AccessLog
} // namespace Http
} // namespace Envoy
