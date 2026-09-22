#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

#include "socket_interface_swap.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#endif

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/network/socket_option_impl.h"

#include "test/integration/filters/stop_and_continue_filter_config.pb.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;

namespace Envoy {
namespace {
std::vector<int> stoiAccessLogString(const std::string& access_log_entry_of_ints) {
  std::vector<int> ret;
  const std::vector<std::string> split_string = TestUtility::split(access_log_entry_of_ints, ' ');
  ret.reserve(split_string.size());

  for (auto& str : split_string) {
    ret.push_back(std::stoi(str));
  }

  return ret;
}

// Helper class that tabulates the bytes of a given stream by consuming the raw HTTP2 frames.
struct StreamByteAccumulator {
  uint32_t stream_wire_bytes_recieved_ = 0;
  uint32_t stream_data_frames_recieved_ = 0;
  uint32_t stream_body_payload_recieved_ = 0;
  uint32_t stream_wire_header_bytes_recieved_ = 0;

  void countFrame(const Http2Frame& frame) {
    stream_wire_bytes_recieved_ += frame.size();
    if (frame.type() == Http2Frame::Type::Data) {
      ++stream_data_frames_recieved_;
      stream_body_payload_recieved_ += frame.payloadSize();
    } else if (frame.type() == Http2Frame::Type::Headers) {
      stream_wire_header_bytes_recieved_ += frame.size();
    }
  }

  int bodyWireBytesReceivedDiscountingHeaders() const {
    return stream_wire_bytes_recieved_ - stream_wire_header_bytes_recieved_;
  }

  int bodyWireBytesReceivedGivenPayloadAndFrames() const {
    return stream_body_payload_recieved_ + stream_data_frames_recieved_ * Http2Frame::HeaderSize;
  }
};

} // end namespace

struct FrameIntegrationTestParam {
  Network::Address::IpVersion ip_version;
  Http2Impl http2_implementation;
};

std::string
frameIntegrationTestParamToString(const testing::TestParamInfo<FrameIntegrationTestParam>& params) {
  return absl::StrCat(TestUtility::ipVersionToString(params.param.ip_version), "_",
                      http2ImplementationToString(params.param.http2_implementation));
}

class Http2FrameIntegrationTest : public testing::TestWithParam<FrameIntegrationTestParam>,
                                  public Http2RawFrameIntegrationTest {
public:
  Http2FrameIntegrationTest() : Http2RawFrameIntegrationTest(GetParam().ip_version) {
    setupHttp2ImplOverrides(GetParam().http2_implementation);
  }

  static std::vector<FrameIntegrationTestParam> testParams() {
    std::vector<FrameIntegrationTestParam> v;
    for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
      v.push_back({ip_version, Http2Impl::Nghttp2});
      v.push_back({ip_version, Http2Impl::Oghttp2});
    }
    return v;
  }

  void sendRequestsAndResponses(uint32_t num_requests) {
    beginSession();

    std::string buffer;
    for (uint32_t i = 0; i < num_requests; ++i) {
      auto request = Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                                 {{"request_no", absl::StrCat(i)}});
      absl::StrAppend(&buffer, std::string(request));
    }

    for (uint32_t i = 0; i < num_requests; ++i) {
      auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a");
      absl::StrAppend(&buffer, std::string(data));
    }

    for (uint32_t i = 0; i < num_requests; ++i) {
      auto trailers = Http2Frame::makeEmptyHeadersFrame(
          Http2Frame::makeClientStreamId(i),
          static_cast<Http2Frame::HeadersFlags>(Http::Http2::orFlags(
              Http2Frame::HeadersFlags::EndStream, Http2Frame::HeadersFlags::EndHeaders)));
      trailers.appendHeaderWithoutIndexing({"k", absl::StrCat("v", i)});
      trailers.adjustPayloadSize();
      absl::StrAppend(&buffer, std::string(trailers));
    }

    ASSERT_TRUE(tcp_client_->write(buffer, false, false));

    waitForNextUpstreamConnection({0}, std::chrono::milliseconds(500), fake_upstream_connection_);
    std::vector<FakeStreamPtr> upstream_requests(num_requests);
    for (uint32_t i = 0; i < num_requests; ++i) {
      FakeStreamPtr upstream_request;
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request));
      ASSERT_TRUE(upstream_request->waitForEndStream(*dispatcher_));
      ASSERT_TRUE(upstream_request->receivedData());
      ASSERT_FALSE(upstream_request->trailers()
                       ->get(Http::LowerCaseString("k"))[0]
                       ->value()
                       .getStringView()
                       .empty());
      upstream_request->encodeHeaders(default_response_headers_, true);
      upstream_requests.push_back(std::move(upstream_request));
    }

    for (uint32_t i = 0; i < num_requests; ++i) {
      auto frame = readFrame();
      EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
      EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
    }
    tcp_client_->close();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2FrameIntegrationTest,
                         testing::ValuesIn(Http2FrameIntegrationTest::testParams()),
                         frameIntegrationTestParamToString);

TEST_P(Http2FrameIntegrationTest, UpstreamRemoteMalformedFrameEndstreamWith1xxHeader) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });
  beginSession();
  FakeRawConnectionPtr fake_upstream_connection;

  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makeRequest(1, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  const Http2Frame settings_frame = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(fake_upstream_connection->write(std::string(settings_frame)));

  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  // A malformed frame is translated to 103 header with END_STREAM by the underlying codec.
  // Typically we should get a protocol error, but this should not crash Envoy.
  // PAYLOAD_LENGTH: \x05
  // FRAME_TYPE: \x01
  // FLAGS: \x32
  // STREAM_ID: \x01
  // ASCII: \x31, \x30, \x33 for 1, 0, 3 respectively
  const std::vector<uint8_t> header_frame = {
      0x00, 0x00, 0x05, 0x01, 0x32, 0x00, 0x00, 0x00, 0x01, 0x2d, 0xfe, 0xff, 0x01, 0x10,
      0x00, 0x00, 0x05, 0x09, 0x0d, 0x00, 0x00, 0x00, 0x01, 0x09, 0x03, 0x31, 0x30, 0x33};
  const std::string header_frame_str(reinterpret_cast<const char*>(header_frame.data()),
                                     header_frame.size());
  ASSERT_TRUE(fake_upstream_connection->write(header_frame_str));

  const Http2Frame response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());

  tcp_client_->close();
  test_server_->waitForGauge("http.config_test.downstream_rq_active", Eq(0));
}

TEST_P(Http2FrameIntegrationTest, MaxConcurrentStreamsIsRespected) {
  const int kTotalRequests = 101;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_max_concurrent_streams()->set_value(100);
      });
  beginSession();

  std::string buffer;
  for (int i = 0; i < kTotalRequests; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/");
    absl::StrAppend(&buffer, std::string(request));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  tcp_client_->waitForDisconnect();
  test_server_->waitForCounter("http.config_test.downstream_cx_destroy_local", Ge(1));
}

// Regression test.
TEST_P(Http2FrameIntegrationTest, SetDetailsTwice) {
  autonomous_upstream_ = true;
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  beginSession();

  // Send two concatenated frames, the first with too many headers, and the second an invalid frame
  // (push_promise)
  std::string bad_frame =
      "00006d0104000000014083a8749783ee3a3fbebebebebebebebebebebebebebebebebebebebebebebebebebebebe"
      "bebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebe"
      "bebebebebebebebebebebebebebebebebebebebebebebebebebe0001010500000000018800a065";
  Http2Frame request = Http2Frame::makeGenericFrameFromHexDump(bad_frame);
  sendFrame(request);
  tcp_client_->close();

  // Expect that the details for the first frame are kept.
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("too_many_headers"));
}

TEST_P(Http2FrameIntegrationTest, UpstreamResponseTrailersWithoutEndStream) {
  beginSession();

  // Downstream → Envoy: header-only GET (END_STREAM|END_HEADERS).
  sendFrame(Http2Frame::makeRequest(1, "host", "/"));

  // Envoy → upstream: grab the raw upstream TCP connection.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Wait until Envoy has actually written its preface + SETTINGS + request
  // HEADERS to the upstream so the client codec stream-1 exists and is
  // half_closed_local (we sent a header-only GET).
  std::string observed;
  ASSERT_TRUE(upstream->waitForData(FakeRawConnection::waitForAtLeastBytes(40), &observed));

  // Craft the malicious upstream response, all in one write so the client
  // codec processes it in a single dispatch():
  //   1. SETTINGS (server preface)
  //   2. SETTINGS ACK
  //   3. HEADERS stream=1 :status=200, END_HEADERS only (no END_STREAM)
  //   4. HEADERS stream=1 trailer "x: y", END_HEADERS only (no END_STREAM)
  //      ← oghttp2 delivers this as RESPONSE_TRAILER without fin; Envoy's
  //        onHeaders() sees flags & END_STREAM == 0 and fires
  //        ASSERT(stream->remote_end_stream_).
  //   5. HEADERS stream=1 trailer again, END_HEADERS|END_STREAM — in a
  //      release build (ASSERT compiled out) step 4 ran decodeTrailers()
  //      and freed the ActiveRequest; this third HEADERS dereferences the
  //      dangling response_decoder_.
  std::string buf;
  absl::StrAppend(&buf, std::string(Http2Frame::makeEmptySettingsFrame()));
  absl::StrAppend(&buf,
                  std::string(Http2Frame::makeEmptySettingsFrame(Http2Frame::SettingsFlags::Ack)));

  // Response headers: :status 200, END_HEADERS only.
  absl::StrAppend(&buf, std::string(Http2Frame::makeHeadersFrameWithStatus(
                            "200", 1, Http2Frame::HeadersFlags::EndHeaders)));

  // Trailers WITHOUT END_STREAM: a single regular header, END_HEADERS only.
  {
    Http2Frame trailers =
        Http2Frame::makeEmptyHeadersFrame(1, Http2Frame::HeadersFlags::EndHeaders);
    trailers.appendHeaderWithoutIndexing(Http2Frame::Header("x", "y"));
    trailers.adjustPayloadSize();
    absl::StrAppend(&buf, std::string(trailers));
  }

  // Third HEADERS (release-build UAF amplifier).
  {
    Http2Frame extra = Http2Frame::makeEmptyHeadersFrame(
        1, static_cast<Http2Frame::HeadersFlags>(Http::Http2::orFlags(
               Http2Frame::HeadersFlags::EndStream, Http2Frame::HeadersFlags::EndHeaders)));
    extra.appendHeaderWithoutIndexing(Http2Frame::Header("x", "z"));
    extra.adjustPayloadSize();
    absl::StrAppend(&buf, std::string(extra));
  }

  ASSERT_TRUE(upstream->write(buf));

  // We expect HEADERS (response headers) first.
  Http2Frame response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());

  // Then we expect RST_STREAM.
  Http2Frame reset = readFrame();
  EXPECT_EQ(Http2Frame::Type::RstStream, reset.type());

  tcp_client_->close();

  if (upstream->connected()) {
    ASSERT_TRUE(upstream->close());
  }
}

TEST_P(Http2FrameIntegrationTest, UpstreamResponseTrailersWithoutEndStream_DataFrameAmplifier) {
  beginSession();

  // Downstream → Envoy: header-only GET (END_STREAM|END_HEADERS).
  sendFrame(Http2Frame::makeRequest(1, "host", "/"));

  // Envoy → upstream: grab the raw upstream TCP connection.
  FakeRawConnectionPtr upstream;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream));

  // Wait until Envoy has actually written its preface + SETTINGS + request
  // HEADERS to the upstream so the client codec stream-1 exists and is
  // half_closed_local (we sent a header-only GET).
  std::string observed;
  ASSERT_TRUE(upstream->waitForData(FakeRawConnection::waitForAtLeastBytes(40), &observed));

  // Craft the malicious upstream response, all in one write so the client
  // codec processes it in a single dispatch():
  //   1. SETTINGS (server preface)
  //   2. SETTINGS ACK
  //   3. HEADERS stream=1 :status=200, END_HEADERS only (no END_STREAM)
  //   4. HEADERS stream=1 trailer "x: y", END_HEADERS only (no END_STREAM)
  //      ← oghttp2 delivers this as RESPONSE_TRAILER without fin; Envoy's
  //        onHeaders() sees flags & END_STREAM == 0 and fires
  //        ASSERT(stream->remote_end_stream_).
  //   5. DATA stream=1 empty, END_STREAM — in a release build (ASSERT
  //      compiled out) step 4 ran decodeTrailers() and freed the ActiveRequest;
  //      this DATA frame dereferences the dangling response_decoder_.
  std::string buf;
  absl::StrAppend(&buf, std::string(Http2Frame::makeEmptySettingsFrame()));
  absl::StrAppend(&buf,
                  std::string(Http2Frame::makeEmptySettingsFrame(Http2Frame::SettingsFlags::Ack)));

  // Response headers: :status 200, END_HEADERS only.
  absl::StrAppend(&buf, std::string(Http2Frame::makeHeadersFrameWithStatus(
                            "200", 1, Http2Frame::HeadersFlags::EndHeaders)));

  // Trailers WITHOUT END_STREAM: a single regular header, END_HEADERS only.
  {
    Http2Frame trailers =
        Http2Frame::makeEmptyHeadersFrame(1, Http2Frame::HeadersFlags::EndHeaders);
    trailers.appendHeaderWithoutIndexing(Http2Frame::Header("x", "y"));
    trailers.adjustPayloadSize();
    absl::StrAppend(&buf, std::string(trailers));
  }

  // Third frame (release-build UAF amplifier): empty DATA frame with END_STREAM.
  {
    Http2Frame extra = Http2Frame::makeEmptyDataFrame(1, Http2Frame::DataFlags::EndStream);
    absl::StrAppend(&buf, std::string(extra));
  }

  ASSERT_TRUE(upstream->write(buf));

  // We expect HEADERS (response headers) first.
  Http2Frame response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());

  // Then we expect RST_STREAM.
  Http2Frame reset = readFrame();
  EXPECT_EQ(Http2Frame::Type::RstStream, reset.type());

  tcp_client_->close();

  if (upstream->connected()) {
    ASSERT_TRUE(upstream->close());
  }
}

TEST_P(Http2FrameIntegrationTest, AdjustUpstreamSettingsMaxStreams) {
  // Configure max concurrent streams to 2.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->mutable_max_concurrent_streams()
        ->set_value(2);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  beginSession();
  FakeRawConnectionPtr fake_upstream_connection1;

  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(1, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1));
  const Http2Frame settings_frame = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1}});
  std::string settings_data(settings_frame);
  ASSERT_TRUE(fake_upstream_connection1->write(settings_data));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_rx_bytes_total",
                               Ge(settings_data.size()));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_total", Eq(1));

  // Start another request, it should create another upstream connection because of the max
  // concurrent streams of upstream connection created above.
  FakeRawConnectionPtr fake_upstream_connection2;
  sendFrame(Http2Frame::makePostRequest(3, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));
  uint64_t bytes_read =
      test_server_->counter("cluster.cluster_0.upstream_cx_rx_bytes_total")->value();
  ASSERT_TRUE(fake_upstream_connection2->write(std::string(settings_frame)));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_rx_bytes_total",
                               Ge(bytes_read + settings_data.size()));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(2));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_total", Eq(2));

  // Adjust the max concurrent streams of one connection created above to 2.
  bytes_read = test_server_->counter("cluster.cluster_0.upstream_cx_rx_bytes_total")->value();
  const Http2Frame settings_frame2 = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 3}});
  std::string settings_data2(settings_frame2);
  ASSERT_TRUE(fake_upstream_connection1->write(settings_data2));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_rx_bytes_total",
                               Ge(bytes_read + settings_data2.size()));
  // Now create another request.
  sendFrame(Http2Frame::makePostRequest(5, "host", "/path/to/long/url"));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(3));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_total", Eq(2));

  // The configured max concurrent streams is 2, even the SETTINGS frame above wants to
  // set the max concurrent streams to 3, it still reaches the upper bound. So the new request
  // below should result in the third connection.
  sendFrame(Http2Frame::makePostRequest(7, "host", "/path/to/long/url"));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(4));
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_total", Eq(3));

  // Cleanup.
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, UpstreamSettingsMaxStreamsAfterGoAway) {
  beginSession();
  FakeRawConnectionPtr fake_upstream_connection;

  const uint32_t client_stream_idx = 1;
  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  const Http2Frame settings_frame = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(fake_upstream_connection->write(std::string(settings_frame)));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  // Send RST_STREAM, GOAWAY and SETTINGS(0 max streams)
  const Http2Frame rst_stream =
      Http2Frame::makeResetStreamFrame(client_stream_idx, Http2Frame::ErrorCode::FlowControlError);
  const Http2Frame go_away_frame =
      Http2Frame::makeEmptyGoAwayFrame(12345, Http2Frame::ErrorCode::NoError);
  const Http2Frame settings_max_connections_frame = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 0}});
  ASSERT_TRUE(fake_upstream_connection->write(
      absl::StrCat(std::string(rst_stream), std::string(go_away_frame),
                   std::string(settings_max_connections_frame))));

  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_close_notify", Ge(1));
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_protocol_error")->value());

  // Cleanup.
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, UpstreamGoAway) {
  beginSession();
  FakeRawConnectionPtr fake_upstream_connection;

  const uint32_t client_stream_idx = 1;
  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  const Http2Frame settings_frame = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(fake_upstream_connection->write(std::string(settings_frame)));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  const Http2Frame rst_stream =
      Http2Frame::makeResetStreamFrame(client_stream_idx, Http2Frame::ErrorCode::FlowControlError);
  const Http2Frame go_away_frame =
      Http2Frame::makeEmptyGoAwayFrame(12345, Http2Frame::ErrorCode::NoError);
  ASSERT_TRUE(fake_upstream_connection->write(
      absl::StrCat(std::string(rst_stream), std::string(go_away_frame))));
  ASSERT_TRUE(fake_upstream_connection->close());

  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_close_notify", Ge(1));
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_protocol_error")->value());

  // Cleanup.
  tcp_client_->close();
}

// Test that sending an invalid frame results in `upstream_cx_protocol_error`.
TEST_P(Http2FrameIntegrationTest, UpstreamProtocolError) {
  beginSession();
  FakeRawConnectionPtr fake_upstream_connection;

  const uint32_t client_stream_idx = 1;
  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  const Http2Frame settings_frame = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(fake_upstream_connection->write(std::string(settings_frame)));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  ASSERT_TRUE(fake_upstream_connection->write("abcdefg this is not a valid h2 frame"));

  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_protocol_error", Ge(1));

  // Cleanup.
  tcp_client_->close();
}

// Verify that receiving a WINDOW_UPDATE frame after a GOAWAY does not trigger an assertion failure
// complaining of continued dispatch after connection close.
TEST_P(Http2FrameIntegrationTest, UpstreamWindowUpdateAfterGoAway) {
  beginSession();
  FakeRawConnectionPtr fake_upstream_connection;

  const uint32_t client_stream_idx = Http2Frame::makeClientStreamId(0);
  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  const Http2Frame settings_frame = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(fake_upstream_connection->write(std::string(settings_frame)));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  // Start a second request and wait for it to reach the upstream. This is to exercise the case
  // where numActiveRequests > 0 at the time that GOAWAY is received from upstream, which is needed
  // to replicate the original assertion failure.
  sendFrame(
      Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(1), "host", "/path/to/long/url"));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(2));

  // Send RST_STREAM, GOAWAY followed by WINDOW_UPDATE
  const Http2Frame rst_stream =
      Http2Frame::makeResetStreamFrame(client_stream_idx, Http2Frame::ErrorCode::FlowControlError);
  // Since last_stream_index <= the stream IDs of all active streams, this
  // results in all active streams being closed, so the connection gets closed
  // as well.
  const Http2Frame go_away_frame = Http2Frame::makeEmptyGoAwayFrame(
      /*last_stream_index=*/client_stream_idx, Http2Frame::ErrorCode::NoError);
  const Http2Frame window_update_frame = Http2Frame::makeWindowUpdateFrame(0, 10);
  ASSERT_TRUE(fake_upstream_connection->write(absl::StrCat(
      std::string(rst_stream), std::string(go_away_frame), std::string(window_update_frame))));

  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_close_notify", Ge(1));

  // Cleanup.
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, AccessLogOfWireBytesIfResponseSizeGreaterThanWindowSize) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        // We need to increase the idle timeout to avoid connection close.
        hcm.mutable_common_http_protocol_options()->mutable_idle_timeout()->set_seconds(10);
      });
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_SENT%");
  beginSession();

  // Sending a settings frame to change window to be less than the response
  // size.
  // Wait for Envoy to ack the renegotiated settings.
  const Http2Frame settings_frame2 = Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None, {{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 70000}});
  sendFrame(settings_frame2);

  auto renegotiated_setting = readFrame();
  EXPECT_EQ(Http2Frame::Type::Settings, renegotiated_setting.type());

  // Start a request and wait for it to reach the upstream.
  sendFrame(Http2Frame::makeRequest(1, "host", "/response/larger/than/window"));
  waitForNextUpstreamRequest();
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(60000, false);
  upstream_request_->encodeData(50000, true);

  // Wire bytes received *ONLY* relates to wire bytes for this stream e.g. connection
  // level frames are irrelevant.
  StreamByteAccumulator accumulator;

  Http2Frame response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());
  accumulator.countFrame(response);

  response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Data, response.type());
  accumulator.countFrame(response);

  response = readFrame();
  accumulator.countFrame(response);
  EXPECT_EQ(Http2Frame::Type::Data, response.type());

  // Check access log if the agnostic stream lifetime is not extended.
  // It should have access logged since it has received the entire response.
  int hcm_logged_wire_bytes_sent, hcm_logged_wire_header_bytes_sent;

  // Grant the sender (Envoy) additional window so it can finish sending the
  // stream.
  const Http2Frame stream_update_frame = Http2Frame::makeWindowUpdateFrame(1, 60000);
  const Http2Frame conn_update_frame = Http2Frame::makeWindowUpdateFrame(0, 60000);
  sendFrame(conn_update_frame);
  sendFrame(stream_update_frame);

  while (!response.endStream() && accumulator.stream_wire_bytes_recieved_ < 60000 + 50000) {
    response = readFrame();
    accumulator.countFrame(response);
    EXPECT_EQ(Http2Frame::Type::Data, response.type());
  }

  EXPECT_EQ(accumulator.bodyWireBytesReceivedDiscountingHeaders(),
            accumulator.bodyWireBytesReceivedGivenPayloadAndFrames());

  // Access logs are only available now due to the expanded agnostic stream
  // lifetime.
  auto access_log_values = stoiAccessLogString(waitForAccessLog(access_log_name_));
  hcm_logged_wire_bytes_sent = access_log_values[0];
  hcm_logged_wire_header_bytes_sent = access_log_values[1];
  EXPECT_EQ(accumulator.stream_wire_header_bytes_recieved_, hcm_logged_wire_header_bytes_sent);
  EXPECT_EQ(accumulator.stream_wire_bytes_recieved_, hcm_logged_wire_bytes_sent)
      << "Received " << accumulator.stream_wire_bytes_recieved_
      << " stream wire bytes from Envoy but access log reported " << hcm_logged_wire_bytes_sent;

  // Cleanup.
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, HostDifferentFromAuthority) {
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx),
                                         "one.example.com", "/path", {{"host", "two.example.com"}});
  sendFrame(request);

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "one.example.com");
  upstream_request_->encodeHeaders(default_response_headers_, true);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, HostSameAsAuthority) {
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx),
                                         "one.example.com", "/path", {{"host", "one.example.com"}});
  sendFrame(request);

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "one.example.com");
  upstream_request_->encodeHeaders(default_response_headers_, true);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, HostConcatenatedWithAuthorityWithOverride) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http2_discard_host_header", "false");
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx),
                                         "one.example.com", "/path", {{"host", "two.example.com"}});
  sendFrame(request);

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "one.example.com,two.example.com");
  upstream_request_->encodeHeaders(default_response_headers_, true);
  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  tcp_client_->close();
}

// All HTTP/2 static headers must be before non-static headers.
// Verify that codecs validate this.
TEST_P(Http2FrameIntegrationTest, HostBeforeAuthorityIsRejected) {
#ifdef ENVOY_ENABLE_UHV
  // TODO(yanavlasov): fix this check for oghttp2 in UHV mode.
  if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
    return;
  }
#endif
  beginSession();

  Http2Frame request = Http2Frame::makeEmptyHeadersFrame(Http2Frame::makeClientStreamId(0),
                                                         Http2Frame::HeadersFlags::EndHeaders);
  request.appendStaticHeader(Http2Frame::StaticHeaderIndex::MethodPost);
  request.appendStaticHeader(Http2Frame::StaticHeaderIndex::SchemeHttps);
  request.appendHeaderWithoutIndexing(Http2Frame::StaticHeaderIndex::Path, "/path");
  // Add the `host` header before `:authority`
  request.appendHeaderWithoutIndexing({"host", "two.example.com"});
  request.appendHeaderWithoutIndexing(Http2Frame::StaticHeaderIndex::Authority, "one.example.com");
  request.adjustPayloadSize();

  sendFrame(request);

  // By default codec treats stream errors as protocol errors and closes the connection.
  tcp_client_->waitForDisconnect();
  tcp_client_->close();
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_cx_protocol_error")->value());
}

TEST_P(Http2FrameIntegrationTest, MultipleHeaderOnlyRequests) {
  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                           {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  }
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleRequests) {
  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request =
        Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                    {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a",
                                          Http2Frame::DataFlags::EndStream);
    absl::StrAppend(&buffer, std::string(data));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  }
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleRequestsWithMetadata) {
  // Allow metadata usage.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->set_allow_metadata(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  config_helper_.prependFilter(R"EOF(
  name: metadata-control-filter
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.MetadataControlFilterConfig
  )EOF");

  const int kRequestsSentPerIOCycle = 20;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request =
        Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                    {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    Http::MetadataMap metadata_map{{"should_continue", absl::StrCat(i)}};
    auto metadata = Http2Frame::makeMetadataFrameFromMetadataMap(
        Http2Frame::makeClientStreamId(i), metadata_map, Http2Frame::MetadataFlags::EndMetadata);
    absl::StrAppend(&buffer, std::string(metadata));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "",
                                          Http2Frame::DataFlags::EndStream);
    absl::StrAppend(&buffer, std::string(data));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  waitForNextUpstreamConnection({0}, std::chrono::milliseconds(500), fake_upstream_connection_);
  std::vector<FakeStreamPtr> upstream_requests(kRequestsSentPerIOCycle);
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    FakeStreamPtr upstream_request;
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request));
    ASSERT_TRUE(upstream_request->waitForEndStream(*dispatcher_));
    ASSERT_TRUE(upstream_request->receivedData());
    upstream_request->encodeHeaders(default_response_headers_, true);
    upstream_requests.push_back(std::move(upstream_request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus());
  }
  tcp_client_->close();
}

// Validate the request completion during processing of deferred list works.
TEST_P(Http2FrameIntegrationTest, MultipleRequestsDecodeHeadersEndsRequest) {
  const int kRequestsSentPerIOCycle = 20;
  // The local-reply-during-decode will call sendLocalReply, completing them
  // when processing headers. This will cause the ConnectionManagerImpl::ActiveRequest
  // object to be removed from the streams_ list during the onDeferredRequestProcessing call.
  config_helper_.addFilter(R"EOF(
    name: local-reply-during-decode
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.LocalReplyDuringDecodeConfig
  )EOF");
  // Process more than 1 deferred request at a time to validate the removal of elements from
  // the list does not break reverse iteration.
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "3");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request =
        Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                    {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a",
                                          Http2Frame::DataFlags::EndStream);
    absl::StrAppend(&buffer, std::string(data));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  // The local-reply-during-decode filter sends 500 status to the client
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    EXPECT_EQ(Http2Frame::ResponseStatus::InternalServerError, frame.responseStatus());
  }
  tcp_client_->close();
}

// Validate that GOAWAY is triggered by a L7 filter.
TEST_P(Http2FrameIntegrationTest, SendGoAwayTriggerredByDecodingFilter) {
  config_helper_.addFilter(R"EOF(
    name: send-goaway-during-decode-filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.SendGoawayFilterConfig
  )EOF");
  beginSession();
  uint32_t num_requests = 10;
  std::string buffer;
  for (uint32_t i = 0; i < num_requests; ++i) {
    auto request = Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                               {{"request_no", absl::StrCat(i)}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a");
    absl::StrAppend(&buffer, std::string(data));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  tcp_client_->waitForDisconnect();
}

// GOAWAY is not triggered by a L7 filter.
TEST_P(Http2FrameIntegrationTest, SendGoAwayNotTriggerredByDecodingFilter) {
  config_helper_.addFilter(R"EOF(
    name: send-goaway-during-decode-filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.SendGoawayFilterConfig
  )EOF");
  beginSession();
  std::string buffer;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(1), "a", "/",
                                         {{"skip-goaway", "true"}});
  absl::StrAppend(&buffer, std::string(request));

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  waitForNextUpstreamConnection({0}, std::chrono::milliseconds(500), fake_upstream_connection_);
  FakeStreamPtr upstream_request;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request));
  ASSERT_TRUE(upstream_request->waitForEndStream(*dispatcher_));
  cleanupUpstreamAndDownstream();
  tcp_client_->close();
}

// Validate that processing of deferred requests with body and trailers is handled correctly
// when there is a filter that pauses and resumes iteration.
TEST_P(Http2FrameIntegrationTest, MultipleRequestsWithTrailersWithFilterChainPause) {
  const int kRequestsSentPerIOCycle = 20;
  // Add filter that stops iteration in the decodeHeaders and resumes in
  // decodeData to verify that downstream end_stream is handled correctly by the filter manager.
  config_helper_.addFilter(R"EOF(
    name: stop-in-headers-continue-in-body-filter
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.StopInHeadersContinueInBodyFilterConfig
  )EOF");
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  sendRequestsAndResponses(kRequestsSentPerIOCycle);
}

TEST_P(Http2FrameIntegrationTest, MultipleRequestsWithTrailersNoPauseInFilterChain) {
  const int kRequestsSentPerIOCycle = 20;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  sendRequestsAndResponses(kRequestsSentPerIOCycle);
}

// Validate the request completion during processing of headers in the deferred requests,
// is ok, when deferred data and trailers are also present.
TEST_P(Http2FrameIntegrationTest, MultipleRequestsWithTrailersDecodeHeadersEndsRequest) {
  const int kRequestsSentPerIOCycle = 20;
  autonomous_upstream_ = true;
  config_helper_.addFilter(R"EOF(
    name: local-reply-during-decode
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.LocalReplyDuringDecodeConfig
  )EOF");
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "6");
  beginSession();

  std::string buffer;
  // Make every 4th request to be reset by the local-reply-during-decode filter, this will give a
  // good distribution of removed requests from the deferred sequence.
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                               {{"response_data_blocks", "0"},
                                                {"no_trailers", "1"},
                                                {"skip-local-reply", i % 4 ? "true" : "false"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto data = Http2Frame::makeDataFrame(Http2Frame::makeClientStreamId(i), "a");
    absl::StrAppend(&buffer, std::string(data));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto trailers = Http2Frame::makeEmptyHeadersFrame(
        Http2Frame::makeClientStreamId(i),
        static_cast<Http2Frame::HeadersFlags>(Http::Http2::orFlags(
            Http2Frame::HeadersFlags::EndStream, Http2Frame::HeadersFlags::EndHeaders)));
    trailers.appendHeaderWithoutIndexing({"k", "v"});
    trailers.adjustPayloadSize();
    absl::StrAppend(&buffer, std::string(trailers));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto frame = readFrame();
    EXPECT_EQ(Http2Frame::Type::Headers, frame.type());
    uint32_t stream_id = frame.streamId();
    // Client stream indexes are multiples of 2 starting at 1
    if ((stream_id / 2) % 4) {
      EXPECT_EQ(Http2Frame::ResponseStatus::Ok, frame.responseStatus())
          << " for stream=" << stream_id;
    } else {
      EXPECT_EQ(Http2Frame::ResponseStatus::InternalServerError, frame.responseStatus())
          << " for stream=" << stream_id;
    }
  }
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, MultipleHeaderOnlyRequestsFollowedByReset) {
  // This number of requests stays below premature reset detection.
  const int kRequestsSentPerIOCycle = 20;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                           {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto reset = Http2Frame::makeResetStreamFrame(Http2Frame::makeClientStreamId(i),
                                                  Http2Frame::ErrorCode::Cancel);
    absl::StrAppend(&buffer, std::string(reset));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  test_server_->waitForCounter("http.config_test.downstream_rq_rx_reset",
                               Eq(kRequestsSentPerIOCycle));
  // Client should remain connected
  ASSERT_TRUE(tcp_client_->connected());
  tcp_client_->close();
}

// This test depends on an another patch with premature resets
TEST_P(Http2FrameIntegrationTest, ResettingDeferredRequestsTriggersPrematureResetCheck) {
  const int kRequestsSentPerIOCycle = 20;
  // Set premature stream count to twice the number of streams we are about to send.
  config_helper_.addRuntimeOverride("overload.premature_reset_total_stream_count", "40");
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/",
                                           {{"response_data_blocks", "0"}, {"no_trailers", "1"}});
    absl::StrAppend(&buffer, std::string(request));
  }

  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto reset = Http2Frame::makeResetStreamFrame(Http2Frame::makeClientStreamId(i),
                                                  Http2Frame::ErrorCode::Cancel);
    absl::StrAppend(&buffer, std::string(reset));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  // Envoy should close the client connection due to too many premature resets
  tcp_client_->waitForDisconnect();
  test_server_->waitForCounter("http.config_test.downstream_rq_too_many_premature_resets", Eq(1));
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, CloseConnectionWithDeferredStreams) {
  // Use large number of requests to ensure close is detected while there are
  // still some deferred streams.
  const int kRequestsSentPerIOCycle = 20000;
  config_helper_.addRuntimeOverride("http.max_requests_per_io_cycle", "1");
  // Ensure premature reset detection does not get in the way
  config_helper_.addRuntimeOverride("overload.premature_reset_total_stream_count", "1001");
  // Disable the request timeout, iouring may failed the test due to request timeout.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_route()
            ->mutable_timeout()
            ->set_seconds(0);
      });
  config_helper_.setDownstreamHttp2MaxConcurrentStreams(20001);
  config_helper_.setUpstreamHttp2MaxConcurrentStreams(20001);

  beginSession();

  std::string buffer;
  for (int i = 0; i < kRequestsSentPerIOCycle; ++i) {
    auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(i), "a", "/");
    absl::StrAppend(&buffer, std::string(request));
  }

  ASSERT_TRUE(tcp_client_->write(buffer, false, false));
  ASSERT_TRUE(tcp_client_->connected());
  // Drop the downstream connection
  tcp_client_->close();
  // Test that Envoy can clean-up deferred streams
  // Make the timeout longer to accommodate non optimized builds
  test_server_->waitForCounter("http.config_test.downstream_rq_rx_reset",
                               Eq(kRequestsSentPerIOCycle), TestUtility::DefaultTimeout * 10);
}

// Tests sending an empty metadata map from downstream.
TEST_P(Http2FrameIntegrationTest, DownstreamSendingEmptyMetadata) {
  // Allow metadata usage.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->set_allow_metadata(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  // This test uses an Http2Frame and not the encoder's encodeMetadata method,
  // because encodeMetadata fails when an empty metadata map is sent.
  beginSession();

  const uint32_t client_stream_idx = 1;
  // Send request.
  const Http2Frame request =
      Http2Frame::makePostRequest(client_stream_idx, "host", "/path/to/long/url");
  sendFrame(request);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send metadata frame with empty metadata map.
  const Http::MetadataMap empty_metadata_map;
  const Http2Frame empty_metadata_map_frame = Http2Frame::makeMetadataFrameFromMetadataMap(
      client_stream_idx, empty_metadata_map, Http2Frame::MetadataFlags::EndMetadata);
  sendFrame(empty_metadata_map_frame);

  // Send an empty data frame to close the stream.
  const Http2Frame empty_data_frame =
      Http2Frame::makeEmptyDataFrame(client_stream_idx, Http2Frame::DataFlags::EndStream);
  sendFrame(empty_data_frame);

  // Upstream sends a reply.
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Make sure that a response from upstream is received by the client, and
  // close the connection.
  const auto response = readFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, response.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, response.responseStatus());
  EXPECT_EQ(1, test_server_->counter("http2.metadata_empty_frames")->value());

  // Cleanup. Closing upstream connection first to avoid a race between the
  // client FIN and the connection closure (see comment in
  // HttpIntegrationTest::cleanupUpstreamAndDownstream).
  cleanupUpstreamAndDownstream();
  tcp_client_->close();
}

TEST_P(Http2FrameIntegrationTest, HostExceedsHeaderMapSizeLimit) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_max_request_headers_kb()->set_value(2); });
  beginSession();

  uint32_t request_idx = 0;
  std::string large_host(2000, 'a');
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx),
                                         "one.example.com", "/path", {{"host", large_host}});
  sendFrame(request);

  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::RstStream, frame.type());
  tcp_client_->close();
  if (GetParam().http2_implementation == Http2Impl::Nghttp2) {
    test_server_->waitForCounter("http2.header_list_size_too_large", testing::Ge(1));
  }
}

TEST_P(Http2FrameIntegrationTest, HostExceedsHeaderMapCountLimit) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(4);
      });
  beginSession();

  uint32_t request_idx = 0;
  auto request = Http2Frame::makeRequest(Http2Frame::makeClientStreamId(request_idx),
                                         "one.example.com", "/path", {{"host", "two.example.com"}});
  sendFrame(request);

  auto frame = readFrame();
  EXPECT_EQ(Http2Frame::Type::RstStream, frame.type());
  tcp_client_->close();
  test_server_->waitForCounter("http2.header_overflow", testing::Ge(1));
}

// RFC 9113 Section 8.1: When the upstream sends a complete gRPC response (HEADERS + DATA +
// trailers with END_STREAM) followed by RST_STREAM(NO_ERROR), and the response body is large
// enough to trigger chunked decoding in the upstream H2 codec (body > connection buffer limit),
// the trailers get buffered. The RST_STREAM(NO_ERROR) is then processed before the buffered
// trailers are delivered, causing the RST_STREAM to reach the router as RemoteReset instead of
// being silently consumed by the preemptive reset in onUpstreamComplete.
// This test verifies that the complete response (including trailers) is forwarded to the client.
TEST_P(Http2FrameIntegrationTest, UpstreamRstStreamNoErrorWithBufferedTrailers) {
  config_helper_.setBufferLimits(1024, 1024);
  beginSession();

  const uint32_t stream_idx = 1;
  sendFrame(Http2Frame::makePostRequest(stream_idx, "host", "/"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->write(std::string(Http2Frame::makeEmptySettingsFrame())));

  // HEADERS (200, EndHeaders, no EndStream)
  const auto response_headers = Http2Frame::makeHeadersFrameWithStatus(
      "200", stream_idx, Http2Frame::HeadersFlags::EndHeaders);

  // DATA: >1024 bytes to trigger chunked decoding (defer_processing_segment_size_ = buffer limit).
  // This causes body_buffered_ = true after the first chunk is decoded, which in turn causes
  // trailers to be buffered via maybeDeferDecodeTrailers().
  const std::string large_body(2000, 'a');
  const auto response_data =
      Http2Frame::makeDataFrame(stream_idx, large_body, Http2Frame::DataFlags::None);

  // Trailers: HEADERS with EndStream | EndHeaders containing "grpc-status: 0".
  std::string hpack_trailers;
  hpack_trailers.push_back(0x00);
  hpack_trailers.push_back(0x0b);
  hpack_trailers.append("grpc-status");
  hpack_trailers.push_back(0x01);
  hpack_trailers.push_back('0');
  const auto trailers =
      Http2Frame::makeRawFrame(Http2Frame::Type::Headers,
                               static_cast<uint8_t>(Http2Frame::HeadersFlags::EndStream) |
                                   static_cast<uint8_t>(Http2Frame::HeadersFlags::EndHeaders),
                               stream_idx, hpack_trailers);

  // RST_STREAM (NO_ERROR)
  const auto rst_stream =
      Http2Frame::makeResetStreamFrame(stream_idx, Http2Frame::ErrorCode::NoError);

  // Send all frames in a single write so they're processed in the same dispatch.
  // The codec will: decode headers (immediate), chunk data (buffer remainder),
  // buffer trailers (body_buffered_), then process RST_STREAM.
  ASSERT_TRUE(fake_upstream_connection->write(
      absl::StrCat(std::string(response_headers), std::string(response_data), std::string(trailers),
                   std::string(rst_stream))));

  auto readNextResponseFrame = [this]() -> Http2Frame {
    while (true) {
      auto frame = readFrame();
      if (frame.type() != Http2Frame::Type::WindowUpdate &&
          frame.type() != Http2Frame::Type::Settings && frame.type() != Http2Frame::Type::Ping) {
        return frame;
      }
    }
  };

  // Response HEADERS — should be 200 OK, not a 503 local error reply.
  auto headers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, headers_frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, headers_frame.responseStatus())
      << "Expected 200 OK from upstream, not a local error reply";

  // Response DATA — may be split across multiple frames due to buffer limits.
  Http2Frame next_frame;
  do {
    next_frame = readNextResponseFrame();
  } while (next_frame.type() == Http2Frame::Type::Data && !next_frame.endStream());

  // Response trailers (HEADERS with END_STREAM) — the complete response must be forwarded.
  // If the bug triggers, we'll get RST_STREAM here instead of trailers.
  auto trailers_frame = next_frame;
  if (trailers_frame.type() == Http2Frame::Type::RstStream) {
    ASSERT_GE(trailers_frame.size(), Http2Frame::HeaderSize + 4);
    const uint8_t* p = trailers_frame.data() + Http2Frame::HeaderSize;
    const uint32_t error_code = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    EXPECT_EQ(Http2Frame::Type::Headers, trailers_frame.type())
        << "Expected trailers (HEADERS with END_STREAM) but got RST_STREAM with error code "
        << error_code
        << ". The RST_STREAM(NO_ERROR) from upstream was likely processed before the buffered "
           "trailers were delivered, causing the response to be truncated.";
  } else {
    EXPECT_EQ(Http2Frame::Type::Headers, trailers_frame.type())
        << "Expected trailers (HEADERS with END_STREAM) but got frame type "
        << static_cast<int>(trailers_frame.type());
    EXPECT_TRUE(trailers_frame.endStream());
  }

  tcp_client_->close();
}

// Verify old behavior when the runtime guard is disabled: RST_STREAM(NO_ERROR) after a complete
// response with buffered trailers causes the response to be truncated (trailers discarded) and
// an error RST_STREAM sent downstream instead.
TEST_P(Http2FrameIntegrationTest, UpstreamRstStreamNoErrorWithBufferedTrailersLegacy) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http_preserve_rst_no_error",
                                    "false");
  config_helper_.setBufferLimits(1024, 1024);
  beginSession();

  const uint32_t stream_idx = 1;
  sendFrame(Http2Frame::makePostRequest(stream_idx, "host", "/"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->write(std::string(Http2Frame::makeEmptySettingsFrame())));

  const auto response_headers = Http2Frame::makeHeadersFrameWithStatus(
      "200", stream_idx, Http2Frame::HeadersFlags::EndHeaders);

  const std::string large_body(2000, 'a');
  const auto response_data =
      Http2Frame::makeDataFrame(stream_idx, large_body, Http2Frame::DataFlags::None);

  std::string hpack_trailers;
  hpack_trailers.push_back(0x00);
  hpack_trailers.push_back(0x0b);
  hpack_trailers.append("grpc-status");
  hpack_trailers.push_back(0x01);
  hpack_trailers.push_back('0');
  const auto trailers =
      Http2Frame::makeRawFrame(Http2Frame::Type::Headers,
                               static_cast<uint8_t>(Http2Frame::HeadersFlags::EndStream) |
                                   static_cast<uint8_t>(Http2Frame::HeadersFlags::EndHeaders),
                               stream_idx, hpack_trailers);

  const auto rst_stream =
      Http2Frame::makeResetStreamFrame(stream_idx, Http2Frame::ErrorCode::NoError);

  ASSERT_TRUE(fake_upstream_connection->write(
      absl::StrCat(std::string(response_headers), std::string(response_data), std::string(trailers),
                   std::string(rst_stream))));

  auto readNextResponseFrame = [this]() -> Http2Frame {
    while (true) {
      auto frame = readFrame();
      if (frame.type() != Http2Frame::Type::WindowUpdate &&
          frame.type() != Http2Frame::Type::Settings && frame.type() != Http2Frame::Type::Ping) {
        return frame;
      }
    }
  };

  auto headers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, headers_frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, headers_frame.responseStatus());

  // Consume DATA frames, then expect RST_STREAM (old buggy behavior: trailers discarded).
  auto next_frame = readNextResponseFrame();
  while (next_frame.type() == Http2Frame::Type::Data) {
    next_frame = readNextResponseFrame();
  }
  EXPECT_EQ(Http2Frame::Type::RstStream, next_frame.type())
      << "With runtime guard disabled, expected RST_STREAM (old behavior) but got frame type "
      << static_cast<int>(next_frame.type());
  ASSERT_GE(next_frame.size(), Http2Frame::HeaderSize + 4);
  const uint8_t* p = next_frame.data() + Http2Frame::HeaderSize;
  const uint32_t error_code = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  EXPECT_NE(0u, error_code) << "With runtime guard disabled, RST_STREAM error code should not be "
                               "NO_ERROR (old behavior sends an error reset)";

  tcp_client_->close();
}

// Verify that RST_STREAM with an actual error code (not NO_ERROR) still correctly resets the stream
// even when trailers are buffered.
TEST_P(Http2FrameIntegrationTest, UpstreamRstStreamWithErrorAndBufferedTrailers) {
  config_helper_.setBufferLimits(1024, 1024);
  beginSession();

  const uint32_t stream_idx = 1;
  sendFrame(Http2Frame::makePostRequest(stream_idx, "host", "/"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->write(std::string(Http2Frame::makeEmptySettingsFrame())));

  const auto response_headers = Http2Frame::makeHeadersFrameWithStatus(
      "200", stream_idx, Http2Frame::HeadersFlags::EndHeaders);

  const std::string large_body(2000, 'a');
  const auto response_data =
      Http2Frame::makeDataFrame(stream_idx, large_body, Http2Frame::DataFlags::None);

  std::string hpack_trailers;
  hpack_trailers.push_back(0x00);
  hpack_trailers.push_back(0x0b);
  hpack_trailers.append("grpc-status");
  hpack_trailers.push_back(0x01);
  hpack_trailers.push_back('0');
  const auto trailers =
      Http2Frame::makeRawFrame(Http2Frame::Type::Headers,
                               static_cast<uint8_t>(Http2Frame::HeadersFlags::EndStream) |
                                   static_cast<uint8_t>(Http2Frame::HeadersFlags::EndHeaders),
                               stream_idx, hpack_trailers);

  // RST_STREAM with INTERNAL_ERROR (not NO_ERROR)
  const auto rst_stream =
      Http2Frame::makeResetStreamFrame(stream_idx, Http2Frame::ErrorCode::InternalError);

  ASSERT_TRUE(fake_upstream_connection->write(
      absl::StrCat(std::string(response_headers), std::string(response_data), std::string(trailers),
                   std::string(rst_stream))));

  auto readNextResponseFrame = [this]() -> Http2Frame {
    while (true) {
      auto frame = readFrame();
      if (frame.type() != Http2Frame::Type::WindowUpdate &&
          frame.type() != Http2Frame::Type::Settings && frame.type() != Http2Frame::Type::Ping) {
        return frame;
      }
    }
  };

  auto headers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, headers_frame.type());

  // With an actual error, the stream should be reset — we expect RST_STREAM downstream,
  // not the complete response with trailers. Consume any DATA frames first.
  auto next_frame = readNextResponseFrame();
  while (next_frame.type() == Http2Frame::Type::Data) {
    next_frame = readNextResponseFrame();
  }
  EXPECT_EQ(Http2Frame::Type::RstStream, next_frame.type())
      << "Expected RST_STREAM for error reset but got frame type "
      << static_cast<int>(next_frame.type());
  ASSERT_GE(next_frame.size(), Http2Frame::HeaderSize + 4);
  const uint8_t* p = next_frame.data() + Http2Frame::HeaderSize;
  const uint32_t error_code = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  EXPECT_NE(0u, error_code) << "RST_STREAM error code should not be NO_ERROR for an error reset";

  tcp_client_->close();
}

// RFC 9113 Section 8.1: A server MAY send RST_STREAM(NO_ERROR) after sending a complete response
// to request that the client stop sending the request body. The client MUST NOT discard the
// response as a result of receiving such a RST_STREAM, and the proxy MUST NOT translate a
// RST_STREAM(NO_ERROR) into an error.
//
// Verify that when the upstream sends a complete response (with END_STREAM on DATA) followed by
// RST_STREAM(NO_ERROR) before the client has sent END_STREAM, Envoy forwards the response and
// sends RST_STREAM(NO_ERROR) downstream (not an error code). Uses raw H2 frames on both sides
// to ensure compliance without depending on Envoy codec interpretation for the upstream and
// downstream.
TEST_P(Http2FrameIntegrationTest, UpstreamCompleteResponseFollowedByRstStreamNoError) {
  beginSession();

  const uint32_t stream_idx = 1;
  sendFrame(Http2Frame::makePostRequest(stream_idx, "host", "/"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->write(std::string(Http2Frame::makeEmptySettingsFrame())));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  const auto response_headers = Http2Frame::makeHeadersFrameWithStatus(
      "200", stream_idx, Http2Frame::HeadersFlags::EndHeaders);
  const auto response_data =
      Http2Frame::makeDataFrame(stream_idx, "hello", Http2Frame::DataFlags::EndStream);
  const auto rst_stream =
      Http2Frame::makeResetStreamFrame(stream_idx, Http2Frame::ErrorCode::NoError);

  ASSERT_TRUE(fake_upstream_connection->write(absl::StrCat(
      std::string(response_headers), std::string(response_data), std::string(rst_stream))));

  auto readNextResponseFrame = [this]() -> Http2Frame {
    while (true) {
      auto frame = readFrame();
      if (frame.type() != Http2Frame::Type::WindowUpdate &&
          frame.type() != Http2Frame::Type::Settings && frame.type() != Http2Frame::Type::Ping) {
        return frame;
      }
    }
  };

  auto headers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, headers_frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, headers_frame.responseStatus())
      << "Expected 200 OK from upstream, not a local error reply";

  auto data_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Data, data_frame.type());
  EXPECT_TRUE(data_frame.endStream());

  auto rst_frame = readNextResponseFrame();
  ASSERT_EQ(Http2Frame::Type::RstStream, rst_frame.type());
  ASSERT_GE(rst_frame.size(), Http2Frame::HeaderSize + 4);
  const uint8_t* p = rst_frame.data() + Http2Frame::HeaderSize;
  const uint32_t error_code = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  EXPECT_EQ(0u, error_code) << "RST_STREAM error code must be NO_ERROR (0) per RFC 9113 "
                               "Section 8.1, but got "
                            << error_code;

  tcp_client_->close();
}

// Same scenario as above, but with allow_multiplexed_upstream_half_close enabled and using
// gRPC-style trailers (HEADERS with END_STREAM) instead of DATA with END_STREAM.
// This exercises the path where the upstream stream stays open after the response is complete
// (onUpstreamComplete returns early), so the server's RST_STREAM(NO_ERROR) reaches the router
// as a RemoteReset via onUpstreamReset. The downstream RST_STREAM must still be NO_ERROR.
TEST_P(Http2FrameIntegrationTest, UpstreamTrailersAndRstStreamNoErrorWithHalfClose) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  beginSession();

  const uint32_t stream_idx = 1;
  sendFrame(Http2Frame::makePostRequest(stream_idx, "host", "/"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->write(std::string(Http2Frame::makeEmptySettingsFrame())));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  // HEADERS (200, EndHeaders, no EndStream)
  const auto response_headers = Http2Frame::makeHeadersFrameWithStatus(
      "200", stream_idx, Http2Frame::HeadersFlags::EndHeaders);

  // DATA ("hello", no EndStream)
  const auto response_data =
      Http2Frame::makeDataFrame(stream_idx, "hello", Http2Frame::DataFlags::None);

  // Trailers: a HEADERS frame with EndStream | EndHeaders, containing "grpc-status: 0".
  // HPACK literal header without indexing, new name:
  //   0x00 | name_len(0x0b) | "grpc-status" | value_len(0x01) | "0"
  std::string hpack_trailers;
  hpack_trailers.push_back(0x00);
  hpack_trailers.push_back(0x0b);
  hpack_trailers.append("grpc-status");
  hpack_trailers.push_back(0x01);
  hpack_trailers.push_back('0');
  const auto trailers =
      Http2Frame::makeRawFrame(Http2Frame::Type::Headers,
                               static_cast<uint8_t>(Http2Frame::HeadersFlags::EndStream) |
                                   static_cast<uint8_t>(Http2Frame::HeadersFlags::EndHeaders),
                               stream_idx, hpack_trailers);

  // RST_STREAM (NO_ERROR)
  const auto rst_stream =
      Http2Frame::makeResetStreamFrame(stream_idx, Http2Frame::ErrorCode::NoError);

  // Send response + RST_STREAM in a single write.
  ASSERT_TRUE(fake_upstream_connection->write(
      absl::StrCat(std::string(response_headers), std::string(response_data), std::string(trailers),
                   std::string(rst_stream))));

  auto readNextResponseFrame = [this]() -> Http2Frame {
    while (true) {
      auto frame = readFrame();
      if (frame.type() != Http2Frame::Type::WindowUpdate &&
          frame.type() != Http2Frame::Type::Settings && frame.type() != Http2Frame::Type::Ping) {
        return frame;
      }
    }
  };

  // Response HEADERS (200 OK).
  auto headers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, headers_frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, headers_frame.responseStatus())
      << "Expected 200 OK from upstream, not a local error reply";

  // Response DATA.
  auto data_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Data, data_frame.type());

  // Response trailers (HEADERS with END_STREAM).
  auto trailers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, trailers_frame.type());
  EXPECT_TRUE(trailers_frame.endStream());

  // RST_STREAM must be NO_ERROR per RFC 9113 Section 8.1.
  auto rst_frame = readNextResponseFrame();
  ASSERT_EQ(Http2Frame::Type::RstStream, rst_frame.type());
  ASSERT_GE(rst_frame.size(), Http2Frame::HeaderSize + 4);
  const uint8_t* p = rst_frame.data() + Http2Frame::HeaderSize;
  const uint32_t error_code = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  EXPECT_EQ(0u, error_code) << "RST_STREAM error code must be NO_ERROR (0) per RFC 9113 "
                               "Section 8.1, but got "
                            << error_code;

  tcp_client_->close();
}

// Same as UpstreamTrailersAndRstStreamNoErrorWithHalfClose, but the RST_STREAM is sent in a
// separate write from the response, simulating separate TCP packets. This tests the case where
// the RST_STREAM(NO_ERROR) arrives in a different event loop iteration from the response.
TEST_P(Http2FrameIntegrationTest, UpstreamTrailersAndSeparateRstStreamNoError) {
  beginSession();

  const uint32_t stream_idx = 1;
  sendFrame(Http2Frame::makePostRequest(stream_idx, "host", "/"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->write(std::string(Http2Frame::makeEmptySettingsFrame())));
  test_server_->waitForGauge("cluster.cluster_0.upstream_rq_active", Eq(1));

  // HEADERS (200, EndHeaders, no EndStream)
  const auto response_headers = Http2Frame::makeHeadersFrameWithStatus(
      "200", stream_idx, Http2Frame::HeadersFlags::EndHeaders);

  // DATA ("hello", no EndStream)
  const auto response_data =
      Http2Frame::makeDataFrame(stream_idx, "hello", Http2Frame::DataFlags::None);

  // Trailers with EndStream | EndHeaders containing "grpc-status: 0".
  std::string hpack_trailers;
  hpack_trailers.push_back(0x00);
  hpack_trailers.push_back(0x0b);
  hpack_trailers.append("grpc-status");
  hpack_trailers.push_back(0x01);
  hpack_trailers.push_back('0');
  const auto trailers =
      Http2Frame::makeRawFrame(Http2Frame::Type::Headers,
                               static_cast<uint8_t>(Http2Frame::HeadersFlags::EndStream) |
                                   static_cast<uint8_t>(Http2Frame::HeadersFlags::EndHeaders),
                               stream_idx, hpack_trailers);

  // Send the response (headers + data + trailers) first.
  ASSERT_TRUE(fake_upstream_connection->write(absl::StrCat(
      std::string(response_headers), std::string(response_data), std::string(trailers))));

  auto readNextResponseFrame = [this]() -> Http2Frame {
    while (true) {
      auto frame = readFrame();
      if (frame.type() != Http2Frame::Type::WindowUpdate &&
          frame.type() != Http2Frame::Type::Settings && frame.type() != Http2Frame::Type::Ping) {
        return frame;
      }
    }
  };

  // Read the response frames first — this implicitly waits for them to be forwarded.
  // Response HEADERS (200 OK).
  auto headers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, headers_frame.type());
  EXPECT_EQ(Http2Frame::ResponseStatus::Ok, headers_frame.responseStatus())
      << "Expected 200 OK from upstream, not a local error reply";

  // Response DATA.
  auto data_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Data, data_frame.type());

  // Response trailers (HEADERS with END_STREAM).
  auto trailers_frame = readNextResponseFrame();
  EXPECT_EQ(Http2Frame::Type::Headers, trailers_frame.type());
  EXPECT_TRUE(trailers_frame.endStream());

  // Now send RST_STREAM(NO_ERROR) in a separate write, after the response is confirmed forwarded.
  const auto rst_stream =
      Http2Frame::makeResetStreamFrame(stream_idx, Http2Frame::ErrorCode::NoError);
  ASSERT_TRUE(fake_upstream_connection->write(std::string(rst_stream)));

  // RST_STREAM must be NO_ERROR.
  auto rst_frame = readNextResponseFrame();
  ASSERT_EQ(Http2Frame::Type::RstStream, rst_frame.type());
  ASSERT_GE(rst_frame.size(), Http2Frame::HeaderSize + 4);
  const uint8_t* p = rst_frame.data() + Http2Frame::HeaderSize;
  const uint32_t error_code = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  EXPECT_EQ(0u, error_code) << "RST_STREAM error code must be NO_ERROR (0) per RFC 9113 "
                               "Section 8.1, but got "
                            << error_code;

  tcp_client_->close();
}

// Verify that sending transfer-encoding header in H/2 protocol causes protocol error.
TEST_P(Http2FrameIntegrationTest, TransferEncodingHeaderIsReset) {
#ifdef ENVOY_ENABLE_UHV
  // TODO(yanavlasov): fix this check for oghttp2 in UHV mode.
  if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
    return;
  }
#endif
  beginSession();

  uint32_t request_idx = 0;
  auto request =
      Http2Frame::makePostRequest(Http2Frame::makeClientStreamId(request_idx), "one.example.com",
                                  "/path", {{"transfer-encoding", "chunked"}});
  sendFrame(request);

  // By default codec treats stream errors as protocol errors and closes the connection.
  tcp_client_->waitForDisconnect();
  tcp_client_->close();
  test_server_->waitForCounter("http.config_test.downstream_cx_protocol_error", testing::Eq(1));
}

} // namespace Envoy
