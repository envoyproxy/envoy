#include <chrono>
#include <functional>

#include "envoy/http/protocol.h"
#include "envoy/upstream/host_description.h"

#include "common/common/fmt.h"
#include "common/protobuf/utility.h"
#include "common/request_info/request_info_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace RequestInfo {
namespace {

std::chrono::nanoseconds checkDuration(std::chrono::nanoseconds last,
                                       absl::optional<std::chrono::nanoseconds> timing) {
  EXPECT_TRUE(timing);
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

  EXPECT_FALSE(info.lastDownstreamRxByteReceived());
  info.onLastDownstreamRxByteReceived();
  std::chrono::nanoseconds dur =
      checkDuration(std::chrono::nanoseconds{0}, info.lastDownstreamRxByteReceived());

  EXPECT_FALSE(info.firstUpstreamTxByteSent());
  info.onFirstUpstreamTxByteSent();
  dur = checkDuration(dur, info.firstUpstreamTxByteSent());

  EXPECT_FALSE(info.lastUpstreamTxByteSent());
  info.onLastUpstreamTxByteSent();
  dur = checkDuration(dur, info.lastUpstreamTxByteSent());

  EXPECT_FALSE(info.firstUpstreamRxByteReceived());
  info.onFirstUpstreamRxByteReceived();
  dur = checkDuration(dur, info.firstUpstreamRxByteReceived());

  EXPECT_FALSE(info.lastUpstreamRxByteReceived());
  info.onLastUpstreamRxByteReceived();
  dur = checkDuration(dur, info.lastUpstreamRxByteReceived());

  EXPECT_FALSE(info.firstDownstreamTxByteSent());
  info.onFirstDownstreamTxByteSent();
  dur = checkDuration(dur, info.firstDownstreamTxByteSent());

  EXPECT_FALSE(info.lastDownstreamTxByteSent());
  info.onLastDownstreamTxByteSent();
  dur = checkDuration(dur, info.lastDownstreamTxByteSent());

  EXPECT_FALSE(info.requestComplete());
  info.onRequestComplete();
  dur = checkDuration(dur, info.requestComplete());
}

TEST(RequestInfoImplTest, BytesTest) {
  RequestInfoImpl request_info(Http::Protocol::Http2);
  const uint64_t bytes_sent = 7;
  const uint64_t bytes_received = 12;

  request_info.addBytesSent(bytes_sent);
  request_info.addBytesReceived(bytes_received);

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
  EXPECT_FALSE(request_info.hasAnyResponseFlag());
  EXPECT_FALSE(request_info.intersectResponseFlags(0));
  for (ResponseFlag flag : responseFlags) {
    // Test cumulative setting of response flags.
    EXPECT_FALSE(request_info.hasResponseFlag(flag))
        << fmt::format("Flag: {} was already set", flag);
    request_info.setResponseFlag(flag);
    EXPECT_TRUE(request_info.hasResponseFlag(flag))
        << fmt::format("Flag: {} was expected to be set", flag);
  }
  EXPECT_TRUE(request_info.hasAnyResponseFlag());

  RequestInfoImpl request_info2(Http::Protocol::Http2);
  request_info2.setResponseFlag(FailedLocalHealthCheck);

  EXPECT_TRUE(request_info2.intersectResponseFlags(FailedLocalHealthCheck));
}

TEST(RequestInfoImplTest, MiscSettersAndGetters) {
  {
    RequestInfoImpl request_info(Http::Protocol::Http2);
    EXPECT_EQ(Http::Protocol::Http2, request_info.protocol().value());

    request_info.protocol(Http::Protocol::Http10);
    EXPECT_EQ(Http::Protocol::Http10, request_info.protocol().value());

    EXPECT_FALSE(request_info.responseCode());
    request_info.response_code_ = 200;
    ASSERT_TRUE(request_info.responseCode());
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

    EXPECT_EQ("", request_info.requestedServerName());
    absl::string_view sni_name = "stubserver.org";
    request_info.setRequestedServerName(sni_name);
    EXPECT_EQ(std::string(sni_name), request_info.requestedServerName());
  }
}

TEST(RequestInfoImplTest, DynamicMetadataTest) {
  RequestInfoImpl request_info(Http::Protocol::Http2);
  EXPECT_EQ(0, request_info.dynamicMetadata().filter_metadata_size());
  request_info.setDynamicMetadata("com.test",
                                  MessageUtil::keyValueStruct("test_key", "test_value"));
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(request_info.dynamicMetadata(), "com.test", "test_key")
                .string_value());
  ProtobufWkt::Struct struct_obj2;
  ProtobufWkt::Value val2;
  val2.set_string_value("another_value");
  (*struct_obj2.mutable_fields())["another_key"] = val2;
  request_info.setDynamicMetadata("com.test", struct_obj2);
  EXPECT_EQ("another_value", Config::Metadata::metadataValue(request_info.dynamicMetadata(),
                                                             "com.test", "another_key")
                                 .string_value());
  // make sure "test_key:test_value" still exists
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(request_info.dynamicMetadata(), "com.test", "test_key")
                .string_value());
  ProtobufTypes::String json;
  const auto test_struct = request_info.dynamicMetadata().filter_metadata().at("com.test");
  const auto status = Protobuf::util::MessageToJsonString(test_struct, &json);
  EXPECT_TRUE(status.ok());
  // check json contains the key and values we set
  EXPECT_TRUE(json.find("\"test_key\":\"test_value\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"another_key\":\"another_value\"") != std::string::npos);
}

} // namespace
} // namespace RequestInfo
} // namespace Envoy
