#include <chrono>
#include <functional>

#include "envoy/http/protocol.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/upstream/host_description.h"

#include "common/common/fmt.h"
#include "common/protobuf/utility.h"
#include "common/stream_info/stream_info_impl.h"

#include "test/common/stream_info/test_int_accessor.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

std::chrono::nanoseconds checkDuration(std::chrono::nanoseconds last,
                                       absl::optional<std::chrono::nanoseconds> timing) {
  EXPECT_TRUE(timing);
  EXPECT_LE(last, timing.value());
  return timing.value();
}

class StreamInfoImplTest : public testing::Test {
protected:
  DangerousDeprecatedTestTime test_time_;
};

TEST_F(StreamInfoImplTest, TimingTest) {
  MonotonicTime pre_start = test_time_.timeSystem().monotonicTime();
  StreamInfoImpl info(Http::Protocol::Http2, test_time_.timeSystem());
  Envoy::StreamInfo::UpstreamTiming upstream_timing;
  MonotonicTime post_start = test_time_.timeSystem().monotonicTime();

  const MonotonicTime& start = info.startTimeMonotonic();

  EXPECT_LE(pre_start, start) << "Start time was lower than expected";
  EXPECT_GE(post_start, start) << "Start time was higher than expected";

  EXPECT_FALSE(info.lastDownstreamRxByteReceived());
  info.onLastDownstreamRxByteReceived();
  std::chrono::nanoseconds dur =
      checkDuration(std::chrono::nanoseconds{0}, info.lastDownstreamRxByteReceived());

  EXPECT_FALSE(info.firstUpstreamTxByteSent());
  upstream_timing.onFirstUpstreamTxByteSent(test_time_.timeSystem());
  info.setUpstreamTiming(upstream_timing);
  dur = checkDuration(dur, info.firstUpstreamTxByteSent());

  EXPECT_FALSE(info.lastUpstreamTxByteSent());
  upstream_timing.onLastUpstreamTxByteSent(test_time_.timeSystem());
  info.setUpstreamTiming(upstream_timing);
  dur = checkDuration(dur, info.lastUpstreamTxByteSent());

  EXPECT_FALSE(info.firstUpstreamRxByteReceived());
  upstream_timing.onFirstUpstreamRxByteReceived(test_time_.timeSystem());
  info.setUpstreamTiming(upstream_timing);
  dur = checkDuration(dur, info.firstUpstreamRxByteReceived());

  EXPECT_FALSE(info.lastUpstreamRxByteReceived());
  upstream_timing.onLastUpstreamRxByteReceived(test_time_.timeSystem());
  info.setUpstreamTiming(upstream_timing);
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

TEST_F(StreamInfoImplTest, BytesTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());

  const uint64_t bytes_sent = 7;
  const uint64_t bytes_received = 12;

  stream_info.addBytesSent(bytes_sent);
  stream_info.addBytesReceived(bytes_received);

  EXPECT_EQ(bytes_sent, stream_info.bytesSent());
  EXPECT_EQ(bytes_received, stream_info.bytesReceived());
}

TEST_F(StreamInfoImplTest, ResponseFlagTest) {
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

  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());

  EXPECT_FALSE(stream_info.hasAnyResponseFlag());
  EXPECT_FALSE(stream_info.intersectResponseFlags(0));
  for (ResponseFlag flag : responseFlags) {
    // Test cumulative setting of response flags.
    EXPECT_FALSE(stream_info.hasResponseFlag(flag))
        << fmt::format("Flag: {} was already set", flag);
    stream_info.setResponseFlag(flag);
    EXPECT_TRUE(stream_info.hasResponseFlag(flag))
        << fmt::format("Flag: {} was expected to be set", flag);
  }
  EXPECT_TRUE(stream_info.hasAnyResponseFlag());
  EXPECT_EQ(0xFFF, stream_info.responseFlags());

  StreamInfoImpl stream_info2(Http::Protocol::Http2, test_time_.timeSystem());
  stream_info2.setResponseFlag(FailedLocalHealthCheck);

  EXPECT_TRUE(stream_info2.intersectResponseFlags(FailedLocalHealthCheck));
}

TEST_F(StreamInfoImplTest, MiscSettersAndGetters) {
  {
    StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());

    EXPECT_EQ(Http::Protocol::Http2, stream_info.protocol().value());

    stream_info.protocol(Http::Protocol::Http10);
    EXPECT_EQ(Http::Protocol::Http10, stream_info.protocol().value());

    EXPECT_FALSE(stream_info.responseCode());
    stream_info.response_code_ = 200;
    ASSERT_TRUE(stream_info.responseCode());
    EXPECT_EQ(200, stream_info.responseCode().value());

    EXPECT_FALSE(stream_info.responseCodeDetails().has_value());
    stream_info.setResponseCodeDetails(ResponseCodeDetails::get().ViaUpstream);
    ASSERT_TRUE(stream_info.responseCodeDetails().has_value());
    EXPECT_EQ(ResponseCodeDetails::get().ViaUpstream, stream_info.responseCodeDetails().value());

    EXPECT_EQ(nullptr, stream_info.upstreamHost());
    Upstream::HostDescriptionConstSharedPtr host(new NiceMock<Upstream::MockHostDescription>());
    stream_info.onUpstreamHostSelected(host);
    EXPECT_EQ(host, stream_info.upstreamHost());

    EXPECT_FALSE(stream_info.healthCheck());
    stream_info.healthCheck(true);
    EXPECT_TRUE(stream_info.healthCheck());

    EXPECT_EQ(nullptr, stream_info.routeEntry());
    NiceMock<Router::MockRouteEntry> route_entry;
    stream_info.route_entry_ = &route_entry;
    EXPECT_EQ(&route_entry, stream_info.routeEntry());

    stream_info.filterState()->setData("test", std::make_unique<TestIntAccessor>(1),
                                       FilterState::StateType::ReadOnly,
                                       FilterState::LifeSpan::FilterChain);
    EXPECT_EQ(1, stream_info.filterState()->getDataReadOnly<TestIntAccessor>("test").access());

    stream_info.setUpstreamFilterState(stream_info.filterState());
    EXPECT_EQ(1,
              stream_info.upstreamFilterState()->getDataReadOnly<TestIntAccessor>("test").access());

    EXPECT_EQ("", stream_info.requestedServerName());
    absl::string_view sni_name = "stubserver.org";
    stream_info.setRequestedServerName(sni_name);
    EXPECT_EQ(std::string(sni_name), stream_info.requestedServerName());

    EXPECT_EQ(absl::nullopt, stream_info.upstreamClusterInfo());
    Upstream::ClusterInfoConstSharedPtr cluster_info(new NiceMock<Upstream::MockClusterInfo>());
    stream_info.setUpstreamClusterInfo(cluster_info);
    EXPECT_NE(absl::nullopt, stream_info.upstreamClusterInfo());
    EXPECT_EQ("fake_cluster", stream_info.upstreamClusterInfo().value()->name());
  }
}

TEST_F(StreamInfoImplTest, DynamicMetadataTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());

  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  stream_info.setDynamicMetadata("com.test", MessageUtil::keyValueStruct("test_key", "test_value"));
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(&stream_info.dynamicMetadata(), "com.test", "test_key")
                .string_value());
  ProtobufWkt::Struct struct_obj2;
  ProtobufWkt::Value val2;
  val2.set_string_value("another_value");
  (*struct_obj2.mutable_fields())["another_key"] = val2;
  stream_info.setDynamicMetadata("com.test", struct_obj2);
  EXPECT_EQ("another_value", Config::Metadata::metadataValue(&stream_info.dynamicMetadata(),
                                                             "com.test", "another_key")
                                 .string_value());
  // make sure "test_key:test_value" still exists
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(&stream_info.dynamicMetadata(), "com.test", "test_key")
                .string_value());
  std::string json;
  const auto test_struct = stream_info.dynamicMetadata().filter_metadata().at("com.test");
  const auto status = Protobuf::util::MessageToJsonString(test_struct, &json);
  EXPECT_TRUE(status.ok());
  // check json contains the key and values we set
  EXPECT_TRUE(json.find("\"test_key\":\"test_value\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"another_key\":\"another_value\"") != std::string::npos);
}

TEST_F(StreamInfoImplTest, DumpStateTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  std::string prefix = "";

  for (int i = 0; i < 7; ++i) {
    std::stringstream out;
    stream_info.dumpState(out, i);
    std::string state = out.str();
    EXPECT_TRUE(absl::StartsWith(state, prefix));
    EXPECT_THAT(state, testing::HasSubstr("protocol_: 2"));
    prefix = prefix + "  ";
  }
}

TEST_F(StreamInfoImplTest, RequestHeadersTest) {
  StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  EXPECT_FALSE(stream_info.getRequestHeaders());

  Http::RequestHeaderMapImpl headers;
  stream_info.setRequestHeaders(headers);
  EXPECT_EQ(&headers, stream_info.getRequestHeaders());
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
