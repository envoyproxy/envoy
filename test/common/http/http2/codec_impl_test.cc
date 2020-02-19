#include <cstdint>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/stats/scope.h"

#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_impl.h"

#include "test/common/http/common.h"
#include "test/common/http/http2/http2_frame.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "codec_impl_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {
namespace Http2 {

using Http2SettingsTuple = ::testing::tuple<uint32_t, uint32_t, uint32_t, uint32_t>;
using Http2SettingsTestParam = ::testing::tuple<Http2SettingsTuple, Http2SettingsTuple>;

class Http2CodecImplTestFixture {
public:
  struct ConnectionWrapper {
    void dispatch(const Buffer::Instance& data, ConnectionImpl& connection) {
      buffer_.add(data);
      if (!dispatching_) {
        while (buffer_.length() > 0) {
          dispatching_ = true;
          connection.dispatch(buffer_);
          dispatching_ = false;
        }
      }
    }

    bool dispatching_{};
    Buffer::OwnedImpl buffer_;
  };

  Http2CodecImplTestFixture(Http2SettingsTuple client_settings, Http2SettingsTuple server_settings)
      : client_settings_(client_settings), server_settings_(server_settings) {}
  virtual ~Http2CodecImplTestFixture() = default;

  virtual void initialize() {
    Http2SettingsFromTuple(client_http2settings_, client_settings_);
    Http2SettingsFromTuple(server_http2settings_, server_settings_);
    client_ = std::make_unique<TestClientConnectionImpl>(
        client_connection_, client_callbacks_, stats_store_, client_http2settings_,
        max_request_headers_kb_, max_response_headers_count_);
    server_ = std::make_unique<TestServerConnectionImpl>(
        server_connection_, server_callbacks_, stats_store_, server_http2settings_,
        max_request_headers_kb_, max_request_headers_count_);

    request_encoder_ = &client_->newStream(response_decoder_);
    setupDefaultConnectionMocks();

    EXPECT_CALL(server_callbacks_, newStream(_, _))
        .WillRepeatedly(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
          response_encoder_ = &encoder;
          encoder.getStream().addCallbacks(server_stream_callbacks_);
          return request_decoder_;
        }));
  }

  void setupDefaultConnectionMocks() {
    ON_CALL(client_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          if (corrupt_metadata_frame_) {
            corruptMetadataFramePayload(data);
          }
          server_wrapper_.dispatch(data, *server_);
        }));
    ON_CALL(server_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          client_wrapper_.dispatch(data, *client_);
        }));
  }

  void Http2SettingsFromTuple(Http2Settings& setting, const Http2SettingsTuple& tp) {
    setting.hpack_table_size_ = ::testing::get<0>(tp);
    setting.max_concurrent_streams_ = ::testing::get<1>(tp);
    setting.initial_stream_window_size_ = ::testing::get<2>(tp);
    setting.initial_connection_window_size_ = ::testing::get<3>(tp);
    setting.allow_metadata_ = allow_metadata_;
    setting.stream_error_on_invalid_http_messaging_ = stream_error_on_invalid_http_messaging_;
    setting.max_outbound_frames_ = max_outbound_frames_;
    setting.max_outbound_control_frames_ = max_outbound_control_frames_;
    setting.max_consecutive_inbound_frames_with_empty_payload_ =
        max_consecutive_inbound_frames_with_empty_payload_;
    setting.max_inbound_priority_frames_per_stream_ = max_inbound_priority_frames_per_stream_;
    setting.max_inbound_window_update_frames_per_data_frame_sent_ =
        max_inbound_window_update_frames_per_data_frame_sent_;
  }

  // corruptMetadataFramePayload assumes data contains at least 10 bytes of the beginning of a
  // frame.
  void corruptMetadataFramePayload(Buffer::Instance& data) {
    const size_t length = data.length();
    const size_t corrupt_start = 10;
    if (length < corrupt_start || length > METADATA_MAX_PAYLOAD_SIZE) {
      ENVOY_LOG_MISC(error, "data size too big or too small");
      return;
    }
    corruptAtOffset(data, corrupt_start, 0xff);
  }

  void corruptAtOffset(Buffer::Instance& data, size_t index, char new_value) {
    if (data.length() == 0) {
      return;
    }
    reinterpret_cast<uint8_t*>(data.linearize(data.length()))[index % data.length()] = new_value;
  }

  const Http2SettingsTuple client_settings_;
  const Http2SettingsTuple server_settings_;
  bool allow_metadata_ = false;
  bool stream_error_on_invalid_http_messaging_ = false;
  Stats::IsolatedStoreImpl stats_store_;
  Http2Settings client_http2settings_;
  NiceMock<Network::MockConnection> client_connection_;
  MockConnectionCallbacks client_callbacks_;
  std::unique_ptr<TestClientConnectionImpl> client_;
  ConnectionWrapper client_wrapper_;
  Http2Settings server_http2settings_;
  NiceMock<Network::MockConnection> server_connection_;
  MockServerConnectionCallbacks server_callbacks_;
  std::unique_ptr<TestServerConnectionImpl> server_;
  ConnectionWrapper server_wrapper_;
  MockResponseDecoder response_decoder_;
  RequestEncoder* request_encoder_;
  MockRequestDecoder request_decoder_;
  ResponseEncoder* response_encoder_{};
  MockStreamCallbacks server_stream_callbacks_;
  // Corrupt a metadata frame payload.
  bool corrupt_metadata_frame_ = false;

  uint32_t max_request_headers_kb_ = Http::DEFAULT_MAX_REQUEST_HEADERS_KB;
  uint32_t max_request_headers_count_ = Http::DEFAULT_MAX_HEADERS_COUNT;
  uint32_t max_response_headers_count_ = Http::DEFAULT_MAX_HEADERS_COUNT;
  uint32_t max_outbound_frames_ = Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES;
  uint32_t max_outbound_control_frames_ = Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES;
  uint32_t max_consecutive_inbound_frames_with_empty_payload_ =
      Http2Settings::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD;
  uint32_t max_inbound_priority_frames_per_stream_ =
      Http2Settings::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM;
  uint32_t max_inbound_window_update_frames_per_data_frame_sent_ =
      Http2Settings::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT;
};

class Http2CodecImplTest : public ::testing::TestWithParam<Http2SettingsTestParam>,
                           protected Http2CodecImplTestFixture {
public:
  Http2CodecImplTest()
      : Http2CodecImplTestFixture(::testing::get<0>(GetParam()), ::testing::get<1>(GetParam())) {}

protected:
  void priorityFlood() {
    initialize();

    TestRequestHeaderMapImpl request_headers;
    HttpTestUtility::addDefaultHeaders(request_headers, "POST");
    EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
    request_encoder_->encodeHeaders(request_headers, false);

    nghttp2_priority_spec spec = {0, 10, 0};
    // HTTP/2 codec adds 1 to the number of active streams when computing PRIORITY frames limit
    constexpr uint32_t max_allowed =
        2 * Http2Settings::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM;
    for (uint32_t i = 0; i < max_allowed + 1; ++i) {
      EXPECT_EQ(0, nghttp2_submit_priority(client_->session(), NGHTTP2_FLAG_NONE, 1, &spec));
    }
  }

  void windowUpdateFlood() {
    initialize();

    TestRequestHeaderMapImpl request_headers;
    HttpTestUtility::addDefaultHeaders(request_headers);
    EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
    request_encoder_->encodeHeaders(request_headers, true);

    // Send one DATA frame back
    EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
    EXPECT_CALL(response_decoder_, decodeData(_, false));
    TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    response_encoder_->encodeHeaders(response_headers, false);
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));

    // See the limit formula in the
    // `Envoy::Http::Http2::ServerConnectionImpl::checkInboundFrameLimits()' method.
    constexpr uint32_t max_allowed =
        1 + 2 * (Http2Settings::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT + 1);
    for (uint32_t i = 0; i < max_allowed + 1; ++i) {
      EXPECT_EQ(0, nghttp2_submit_window_update(client_->session(), NGHTTP2_FLAG_NONE, 1, 1));
    }
  }

  void emptyDataFlood(Buffer::OwnedImpl& data) {
    initialize();

    TestRequestHeaderMapImpl request_headers;
    HttpTestUtility::addDefaultHeaders(request_headers, "POST");
    EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
    request_encoder_->encodeHeaders(request_headers, false);

    // HTTP/2 codec does not send empty DATA frames with no END_STREAM flag.
    // To make this work, send raw bytes representing empty DATA frames bypassing client codec.
    Http2Frame emptyDataFrame = Http2Frame::makeEmptyDataFrame(0);
    constexpr uint32_t max_allowed =
        Http2Settings::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD;
    for (uint32_t i = 0; i < max_allowed + 1; ++i) {
      data.add(emptyDataFrame.data(), emptyDataFrame.size());
    }
  }
};

TEST_P(Http2CodecImplTest, ShutdownNotice) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  EXPECT_CALL(client_callbacks_, onGoAway());
  server_->shutdownNotice();
  server_->goAway();

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
}

TEST_P(Http2CodecImplTest, ContinueHeaders) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
};

TEST_P(Http2CodecImplTest, InvalidContinueWithFin) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_THROW(response_encoder_->encodeHeaders(continue_headers, true), CodecProtocolException);
  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
}

TEST_P(Http2CodecImplTest, InvalidContinueWithFinAllowed) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  // Buffer client data to avoid mock recursion causing lifetime issues.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  response_encoder_->encodeHeaders(continue_headers, true);

  // Flush pending data.
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::LocalReset, _));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);

  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
}

TEST_P(Http2CodecImplTest, InvalidRepeatContinue) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  EXPECT_THROW(response_encoder_->encodeHeaders(continue_headers, true), CodecProtocolException);
  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, InvalidRepeatContinueAllowed) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  // Buffer client data to avoid mock recursion causing lifetime issues.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));

  response_encoder_->encodeHeaders(continue_headers, true);

  // Flush pending data.
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::LocalReset, _));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);

  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, Invalid103) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  TestResponseHeaderMapImpl early_hint_headers{{":status", "103"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(early_hint_headers, false);

  EXPECT_THROW_WITH_MESSAGE(response_encoder_->encodeHeaders(early_hint_headers, false),
                            CodecProtocolException, "Unexpected 'trailers' with no end stream.");
  EXPECT_EQ(1, stats_store_.counter("http2.too_many_header_frames").value());
}

TEST_P(Http2CodecImplTest, Invalid204WithContentLength) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestResponseHeaderMapImpl response_headers{{":status", "204"}, {"content-length", "3"}};
  // What follows is a hack to get headers that should span into continuation frames. The default
  // maximum frame size is 16K. We will add 3,000 headers that will take us above this size and
  // not easily compress with HPACK. (I confirmed this generates 26,468 bytes of header data
  // which should contain a continuation.)
  for (unsigned i = 1; i < 3000; i++) {
    response_headers.addCopy(std::to_string(i), std::to_string(i));
  }

  EXPECT_THROW(response_encoder_->encodeHeaders(response_headers, false), CodecProtocolException);
  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, Invalid204WithContentLengthAllowed) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  // Buffer client data to avoid mock recursion causing lifetime issues.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));

  TestResponseHeaderMapImpl response_headers{{":status", "204"}, {"content-length", "3"}};
  // What follows is a hack to get headers that should span into continuation frames. The default
  // maximum frame size is 16K. We will add 3,000 headers that will take us above this size and
  // not easily compress with HPACK. (I confirmed this generates 26,468 bytes of header data
  // which should contain a continuation.)
  for (int i = 1; i < 3000; i++) {
    response_headers.addCopy(std::to_string(i), std::to_string(i));
  }

  response_encoder_->encodeHeaders(response_headers, false);

  // Flush pending data.
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::LocalReset, _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::RemoteReset, _));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);

  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, RefusedStreamReset) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);
  EXPECT_CALL(server_stream_callbacks_,
              onResetStream(StreamResetReason::LocalRefusedStreamReset, _));
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteRefusedStreamReset, _));
  response_encoder_->getStream().resetStream(StreamResetReason::LocalRefusedStreamReset);
}

TEST_P(Http2CodecImplTest, InvalidHeadersFrame) {
  initialize();

  EXPECT_THROW(request_encoder_->encodeHeaders(TestRequestHeaderMapImpl{}, true),
               CodecProtocolException);
  EXPECT_EQ(1, stats_store_.counter("http2.rx_messaging_error").value());
}

TEST_P(Http2CodecImplTest, InvalidHeadersFrameAllowed) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));

  request_encoder_->encodeHeaders(TestRequestHeaderMapImpl{}, true);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::LocalReset, _));
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::RemoteReset, _));
  server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);
}

TEST_P(Http2CodecImplTest, TrailingHeaders) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder_, decodeData(_, false));
  Buffer::OwnedImpl hello("hello");
  request_encoder_->encodeData(hello, false);
  EXPECT_CALL(request_decoder_, decodeTrailers_(_));
  request_encoder_->encodeTrailers(TestRequestTrailerMapImpl{{"trailing", "header"}});

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder_->encodeData(world, false);
  EXPECT_CALL(response_decoder_, decodeTrailers_(_));
  response_encoder_->encodeTrailers(TestResponseTrailerMapImpl{{"trailing", "header"}});
}

TEST_P(Http2CodecImplTest, TrailingHeadersLargeBody) {
  initialize();

  // Buffer server data so we can make sure we don't get any window updates.
  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AtLeast(1));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  request_encoder_->encodeData(body, false);
  EXPECT_CALL(request_decoder_, decodeTrailers_(_));
  request_encoder_->encodeTrailers(TestRequestTrailerMapImpl{{"trailing", "header"}});

  // Flush pending data.
  setupDefaultConnectionMocks();
  server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder_->encodeData(world, false);
  EXPECT_CALL(response_decoder_, decodeTrailers_(_));
  response_encoder_->encodeTrailers(TestResponseTrailerMapImpl{{"trailing", "header"}});
}

TEST_P(Http2CodecImplTest, SmallMetadataVecTest) {
  allow_metadata_ = true;
  initialize();

  // Generates a valid stream_id by sending a request header.
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  MetadataMapVector metadata_map_vector;
  const int size = 10;
  for (int i = 0; i < size; i++) {
    MetadataMap metadata_map = {
        {"header_key1", "header_value1"},
        {"header_key2", "header_value2"},
        {"header_key3", "header_value3"},
        {"header_key4", "header_value4"},
    };
    MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
    metadata_map_vector.push_back(std::move(metadata_map_ptr));
  }

  EXPECT_CALL(request_decoder_, decodeMetadata_(_)).Times(size);
  request_encoder_->encodeMetadata(metadata_map_vector);

  EXPECT_CALL(response_decoder_, decodeMetadata_(_)).Times(size);
  response_encoder_->encodeMetadata(metadata_map_vector);
}

TEST_P(Http2CodecImplTest, LargeMetadataVecTest) {
  allow_metadata_ = true;
  initialize();

  // Generates a valid stream_id by sending a request header.
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  MetadataMapVector metadata_map_vector;
  const int size = 10;
  for (int i = 0; i < size; i++) {
    MetadataMap metadata_map = {
        {"header_key1", std::string(50 * 1024, 'a')},
    };
    MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
    metadata_map_vector.push_back(std::move(metadata_map_ptr));
  }

  EXPECT_CALL(request_decoder_, decodeMetadata_(_)).Times(size);
  request_encoder_->encodeMetadata(metadata_map_vector);

  EXPECT_CALL(response_decoder_, decodeMetadata_(_)).Times(size);
  response_encoder_->encodeMetadata(metadata_map_vector);
}

TEST_P(Http2CodecImplTest, BadMetadataVecReceivedTest) {
  allow_metadata_ = true;
  initialize();

  // Generates a valid stream_id by sending a request header.
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
      {"header_key3", "header_value3"},
      {"header_key4", "header_value4"},
  };
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  corrupt_metadata_frame_ = true;
  EXPECT_THROW_WITH_MESSAGE(request_encoder_->encodeMetadata(metadata_map_vector), EnvoyException,
                            "The user callback function failed");
}
class Http2CodecImplDeferredResetTest : public Http2CodecImplTest {};

TEST_P(Http2CodecImplDeferredResetTest, DeferredResetClient) {
  initialize();

  InSequence s;

  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);

  // Do a request, but pause server dispatch so we don't send window updates. This will result in a
  // deferred reset, followed by a pending frames flush which will cause the stream to actually
  // be reset immediately since we are outside of dispatch context.
  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  request_encoder_->encodeHeaders(request_headers, false);
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  EXPECT_CALL(client_stream_callbacks, onAboveWriteBufferHighWatermark()).Times(AnyNumber());
  request_encoder_->encodeData(body, true);
  EXPECT_CALL(client_stream_callbacks, onResetStream(StreamResetReason::LocalReset, _));
  request_encoder_->getStream().resetStream(StreamResetReason::LocalReset);

  // Dispatch server. We expect to see some data.
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false)).WillOnce(InvokeWithoutArgs([&]() -> void {
    // Start a response inside the headers callback. This should not result in the client
    // seeing any headers as the stream should already be reset on the other side, even though
    // we don't know about it yet.
    TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    response_encoder_->encodeHeaders(response_headers, false);
  }));
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AtLeast(1));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::RemoteReset, _));

  setupDefaultConnectionMocks();
  server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);
}

TEST_P(Http2CodecImplDeferredResetTest, DeferredResetServer) {
  initialize();

  InSequence s;

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  // In this case we do the same thing as DeferredResetClient but on the server side.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark()).Times(AnyNumber());
  response_encoder_->encodeData(body, true);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::LocalReset, _));
  response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);

  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  EXPECT_CALL(client_stream_callbacks, onResetStream(StreamResetReason::RemoteReset, _));
  setupDefaultConnectionMocks();
  client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);
}

class Http2CodecImplFlowControlTest : public Http2CodecImplTest {};

// Back up the pending_sent_data_ buffer in the client connection and make sure the watermarks fire
// as expected.
//
// This also tests the readDisable logic in StreamImpl, verifying that h2 bytes are consumed
// when the stream has readDisable(true) called.
TEST_P(Http2CodecImplFlowControlTest, TestFlowControlInPendingSendData) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Force the server stream to be read disabled. This will cause it to stop sending window
  // updates to the client.
  server_->getStream(1)->readDisable(true);

  uint32_t initial_stream_window =
      nghttp2_session_get_stream_effective_local_window_size(client_->session(), 1);
  // If this limit is changed, this test will fail due to the initial large writes being divided
  // into more than 4 frames. Fast fail here with this explanatory comment.
  ASSERT_EQ(65535, initial_stream_window);
  // Make sure the limits were configured properly in test set up.
  EXPECT_EQ(initial_stream_window, server_->getStream(1)->bufferLimit());
  EXPECT_EQ(initial_stream_window, client_->getStream(1)->bufferLimit());

  // One large write gets broken into smaller frames.
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AnyNumber());
  Buffer::OwnedImpl long_data(std::string(initial_stream_window, 'a'));
  request_encoder_->encodeData(long_data, false);

  // Verify that the window is full. The client will not send more data to the server for this
  // stream.
  EXPECT_EQ(0, nghttp2_session_get_stream_local_window_size(server_->session(), 1));
  EXPECT_EQ(0, nghttp2_session_get_stream_remote_window_size(client_->session(), 1));
  EXPECT_EQ(initial_stream_window, server_->getStream(1)->unconsumed_bytes_);

  // Now that the flow control window is full, further data causes the send buffer to back up.
  Buffer::OwnedImpl more_long_data(std::string(initial_stream_window, 'a'));
  request_encoder_->encodeData(more_long_data, false);
  EXPECT_EQ(initial_stream_window, client_->getStream(1)->pending_send_data_.length());
  EXPECT_EQ(initial_stream_window, server_->getStream(1)->unconsumed_bytes_);

  // If we go over the limit, the stream callbacks should fire.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  Buffer::OwnedImpl last_byte("!");
  request_encoder_->encodeData(last_byte, false);
  EXPECT_EQ(initial_stream_window + 1, client_->getStream(1)->pending_send_data_.length());

  // Now create a second stream on the connection.
  MockStreamDecoder response_decoder2;
  RequestEncoder* request_encoder2 = &client_->newStream(response_decoder_);
  StreamEncoder* response_encoder2;
  MockStreamCallbacks server_stream_callbacks2;
  MockRequestDecoder request_decoder2;
  // When the server stream is created it should check the status of the
  // underlying connection. Pretend it is overrun.
  EXPECT_CALL(server_connection_, aboveHighWatermark()).WillOnce(Return(true));
  EXPECT_CALL(server_stream_callbacks2, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(server_callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder2 = &encoder;
        encoder.getStream().addCallbacks(server_stream_callbacks2);
        return request_decoder2;
      }));
  EXPECT_CALL(request_decoder2, decodeHeaders_(_, false));
  request_encoder2->encodeHeaders(request_headers, false);

  // Add the stream callbacks belatedly. On creation the stream should have
  // been noticed that the connection was backed up. Any new subscriber to
  // stream callbacks should get a callback when they addCallbacks.
  MockStreamCallbacks callbacks2;
  EXPECT_CALL(callbacks2, onAboveWriteBufferHighWatermark());
  request_encoder_->getStream().addCallbacks(callbacks2);

  // Add a third callback to make testing removal mid-watermark call below more interesting.
  MockStreamCallbacks callbacks3;
  EXPECT_CALL(callbacks3, onAboveWriteBufferHighWatermark());
  request_encoder_->getStream().addCallbacks(callbacks3);

  // Now unblock the server's stream. This will cause the bytes to be consumed, flow control
  // updates to be sent, and the client to flush all queued data.
  // For bonus corner case coverage, remove callback2 in the middle of runLowWatermarkCallbacks()
  // and ensure it is not called.
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).WillOnce(Invoke([&]() -> void {
    request_encoder_->getStream().removeCallbacks(callbacks2);
  }));
  EXPECT_CALL(callbacks2, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(callbacks3, onBelowWriteBufferLowWatermark());
  server_->getStream(1)->readDisable(false);
  EXPECT_EQ(0, client_->getStream(1)->pending_send_data_.length());
  // The extra 1 byte sent won't trigger another window update, so the final window should be the
  // initial window minus the last 1 byte flush from the client to server.
  EXPECT_EQ(initial_stream_window - 1,
            nghttp2_session_get_stream_local_window_size(server_->session(), 1));
  EXPECT_EQ(initial_stream_window - 1,
            nghttp2_session_get_stream_remote_window_size(client_->session(), 1));
}

// Set up the same asTestFlowControlInPendingSendData, but tears the stream down with an early reset
// once the flow control window is full up.
TEST_P(Http2CodecImplFlowControlTest, EarlyResetRestoresWindow) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Force the server stream to be read disabled. This will cause it to stop sending window
  // updates to the client.
  server_->getStream(1)->readDisable(true);

  uint32_t initial_stream_window =
      nghttp2_session_get_stream_effective_local_window_size(client_->session(), 1);
  uint32_t initial_connection_window = nghttp2_session_get_remote_window_size(client_->session());
  // If this limit is changed, this test will fail due to the initial large writes being divided
  // into more than 4 frames. Fast fail here with this explanatory comment.
  ASSERT_EQ(65535, initial_stream_window);
  // One large write may get broken into smaller frames.
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AnyNumber());
  Buffer::OwnedImpl long_data(std::string(initial_stream_window, 'a'));
  // The one giant write will cause the buffer to go over the limit, then drain and go back under
  // the limit.
  request_encoder_->encodeData(long_data, false);

  // Verify that the window is full. The client will not send more data to the server for this
  // stream.
  EXPECT_EQ(0, nghttp2_session_get_stream_local_window_size(server_->session(), 1));
  EXPECT_EQ(0, nghttp2_session_get_stream_remote_window_size(client_->session(), 1));
  EXPECT_EQ(initial_stream_window, server_->getStream(1)->unconsumed_bytes_);
  EXPECT_GT(initial_connection_window, nghttp2_session_get_remote_window_size(client_->session()));

  EXPECT_CALL(server_stream_callbacks_,
              onResetStream(StreamResetReason::LocalRefusedStreamReset, _));
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteRefusedStreamReset, _))
      .WillOnce(Invoke([&](StreamResetReason, absl::string_view) -> void {
        // Test the case where the reset callbacks cause the socket to fill up,
        // causing the underlying connection to back up. Given the stream is
        // being destroyed the watermark callbacks should not fire (mocks for Times(0)
        // above)
        client_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
        client_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
        server_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
        server_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
      }));
  response_encoder_->getStream().resetStream(StreamResetReason::LocalRefusedStreamReset);

  // Regression test that the window is consumed even if the stream is destroyed early.
  EXPECT_EQ(initial_connection_window, nghttp2_session_get_remote_window_size(client_->session()));
}

// Test the HTTP2 pending_recv_data_ buffer going over and under watermark limits.
TEST_P(Http2CodecImplFlowControlTest, FlowControlPendingRecvData) {
  initialize();
  MockStreamCallbacks callbacks;

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Set artificially small watermarks to make the recv buffer easy to overrun. In production,
  // the recv buffer can be overrun by a client which negotiates a larger
  // SETTINGS_MAX_FRAME_SIZE but there's no current easy way to tweak that in
  // envoy (without sending raw HTTP/2 frames) so we lower the buffer limit instead.
  server_->getStream(1)->setWriteBufferWatermarks(10, 20);

  EXPECT_CALL(request_decoder_, decodeData(_, false));
  Buffer::OwnedImpl data(std::string(40, 'a'));
  request_encoder_->encodeData(data, false);
}

TEST_P(Http2CodecImplTest, WatermarkUnderEndStream) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  // The 'true' on encodeData will set local_end_stream_ on the client but not
  // the server. Verify that client watermark callbacks will not be called, but
  // server callbacks may be called by simulating connection overflow on both
  // ends.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(server_stream_callbacks_, onBelowWriteBufferLowWatermark());
  EXPECT_CALL(request_decoder_, decodeData(_, true)).WillOnce(InvokeWithoutArgs([&]() -> void {
    client_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
    client_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
    server_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
    server_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }));
  Buffer::OwnedImpl hello("hello");
  request_encoder_->encodeData(hello, true);

  // The 'true' on encodeData will set local_end_stream_ on the server. Verify
  // that neither client nor server watermark callbacks will be called again.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(server_stream_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(HeaderMapEqual(&response_headers), true))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        client_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
        client_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
        server_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
        server_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
      }));
  response_encoder_->encodeHeaders(response_headers, true);
}

class Http2CodecImplStreamLimitTest : public Http2CodecImplTest {};

// Regression test for issue #3076.
//
// TODO(PiotrSikora): add tests that exercise both scenarios: before and after receiving
// the HTTP/2 SETTINGS frame.
TEST_P(Http2CodecImplStreamLimitTest, MaxClientStreams) {
  Http2SettingsFromTuple(client_http2settings_, ::testing::get<0>(GetParam()));
  Http2SettingsFromTuple(server_http2settings_, ::testing::get<1>(GetParam()));
  client_ = std::make_unique<TestClientConnectionImpl>(
      client_connection_, client_callbacks_, stats_store_, client_http2settings_,
      max_request_headers_kb_, max_response_headers_count_);
  server_ = std::make_unique<TestServerConnectionImpl>(
      server_connection_, server_callbacks_, stats_store_, server_http2settings_,
      max_request_headers_kb_, max_request_headers_count_);

  for (int i = 0; i < 101; ++i) {
    request_encoder_ = &client_->newStream(response_decoder_);
    setupDefaultConnectionMocks();
    EXPECT_CALL(server_callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
          response_encoder_ = &encoder;
          encoder.getStream().addCallbacks(server_stream_callbacks_);
          return request_decoder_;
        }));

    TestRequestHeaderMapImpl request_headers;
    HttpTestUtility::addDefaultHeaders(request_headers);
    EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
    request_encoder_->encodeHeaders(request_headers, true);
  }
}

#define HTTP2SETTINGS_SMALL_WINDOW_COMBINE                                                         \
  ::testing::Combine(::testing::Values(Http2Settings::DEFAULT_HPACK_TABLE_SIZE),                   \
                     ::testing::Values(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS),             \
                     ::testing::Values(Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE),             \
                     ::testing::Values(Http2Settings::MIN_INITIAL_CONNECTION_WINDOW_SIZE))

// Deferred reset tests use only small windows so that we can test certain conditions.
INSTANTIATE_TEST_SUITE_P(Http2CodecImplDeferredResetTest, Http2CodecImplDeferredResetTest,
                         ::testing::Combine(HTTP2SETTINGS_SMALL_WINDOW_COMBINE,
                                            HTTP2SETTINGS_SMALL_WINDOW_COMBINE));

// Flow control tests only use only small windows so that we can test certain conditions.
INSTANTIATE_TEST_SUITE_P(Http2CodecImplFlowControlTest, Http2CodecImplFlowControlTest,
                         ::testing::Combine(HTTP2SETTINGS_SMALL_WINDOW_COMBINE,
                                            HTTP2SETTINGS_SMALL_WINDOW_COMBINE));

// we separate default/edge cases here to avoid combinatorial explosion
#define HTTP2SETTINGS_DEFAULT_COMBINE                                                              \
  ::testing::Combine(::testing::Values(Http2Settings::DEFAULT_HPACK_TABLE_SIZE),                   \
                     ::testing::Values(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS),             \
                     ::testing::Values(Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE),         \
                     ::testing::Values(Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE))

// Stream limit test only uses the default values because not all combinations of
// edge settings allow for the number of streams needed by the test.
INSTANTIATE_TEST_SUITE_P(Http2CodecImplStreamLimitTest, Http2CodecImplStreamLimitTest,
                         ::testing::Combine(HTTP2SETTINGS_DEFAULT_COMBINE,
                                            HTTP2SETTINGS_DEFAULT_COMBINE));

INSTANTIATE_TEST_SUITE_P(Http2CodecImplTestDefaultSettings, Http2CodecImplTest,
                         ::testing::Combine(HTTP2SETTINGS_DEFAULT_COMBINE,
                                            HTTP2SETTINGS_DEFAULT_COMBINE));

#define HTTP2SETTINGS_EDGE_COMBINE                                                                 \
  ::testing::Combine(                                                                              \
      ::testing::Values(Http2Settings::MIN_HPACK_TABLE_SIZE, Http2Settings::MAX_HPACK_TABLE_SIZE), \
      ::testing::Values(Http2Settings::MIN_MAX_CONCURRENT_STREAMS,                                 \
                        Http2Settings::MAX_MAX_CONCURRENT_STREAMS),                                \
      ::testing::Values(Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE,                             \
                        Http2Settings::MAX_INITIAL_STREAM_WINDOW_SIZE),                            \
      ::testing::Values(Http2Settings::MIN_INITIAL_CONNECTION_WINDOW_SIZE,                         \
                        Http2Settings::MAX_INITIAL_CONNECTION_WINDOW_SIZE))

// Make sure we have coverage for high and low values for various  combinations and permutations
// of HTTP settings in at least one test fixture.
// Use with caution as any test using this runs 255 times.
using Http2CodecImplTestAll = Http2CodecImplTest;

INSTANTIATE_TEST_SUITE_P(Http2CodecImplTestDefaultSettings, Http2CodecImplTestAll,
                         ::testing::Combine(HTTP2SETTINGS_DEFAULT_COMBINE,
                                            HTTP2SETTINGS_DEFAULT_COMBINE));
INSTANTIATE_TEST_SUITE_P(Http2CodecImplTestEdgeSettings, Http2CodecImplTestAll,
                         ::testing::Combine(HTTP2SETTINGS_EDGE_COMBINE,
                                            HTTP2SETTINGS_EDGE_COMBINE));

TEST(Http2CodecUtility, reconstituteCrumbledCookies) {
  {
    HeaderString key;
    HeaderString value;
    HeaderString cookies;
    EXPECT_FALSE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_TRUE(cookies.empty());
  }

  {
    HeaderString key(Headers::get().ContentLength);
    HeaderString value;
    value.setInteger(5);
    HeaderString cookies;
    EXPECT_FALSE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_TRUE(cookies.empty());
  }

  {
    HeaderString key(Headers::get().Cookie);
    HeaderString value;
    value.setCopy("a=b", 3);
    HeaderString cookies;
    EXPECT_TRUE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_EQ(cookies, "a=b");

    HeaderString key2(Headers::get().Cookie);
    HeaderString value2;
    value2.setCopy("c=d", 3);
    EXPECT_TRUE(Utility::reconstituteCrumbledCookies(key2, value2, cookies));
    EXPECT_EQ(cookies, "a=b; c=d");
  }
}

// Tests request headers whose size is larger than the default limit of 60K.
TEST_P(Http2CodecImplTest, LargeRequestHeadersInvokeResetStream) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(63 * 1024, 'q');
  request_headers.addCopy("big", long_string);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(1);
  request_encoder_->encodeHeaders(request_headers, false);
}

// Large request headers are accepted when max limit configured.
TEST_P(Http2CodecImplTest, LargeRequestHeadersAccepted) {
  max_request_headers_kb_ = 64;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(63 * 1024, 'q');
  request_headers.addCopy("big", long_string);

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  request_encoder_->encodeHeaders(request_headers, false);
}

// This is the HTTP/2 variant of the HTTP/1 regression test for CVE-2019-18801.
// Large method headers should not trigger ASSERTs or ASAN. The underlying issue
// in CVE-2019-18801 only affected the HTTP/1 encoder, but we include a test
// here for belt-and-braces. This also demonstrates that the HTTP/2 codec will
// accept arbitrary :method headers, unlike the HTTP/1 codec (see
// Http1ServerConnectionImplTest.RejectInvalidMethod for comparison).
TEST_P(Http2CodecImplTest, LargeMethodRequestEncode) {
  max_request_headers_kb_ = 80;
  initialize();

  const std::string long_method = std::string(79 * 1024, 'a');
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  request_headers.setReferenceKey(Headers::get().Method, long_method);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&request_headers), false));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  request_encoder_->encodeHeaders(request_headers, false);
}

// Tests stream reset when the number of request headers exceeds the default maximum of 100.
TEST_P(Http2CodecImplTest, ManyRequestHeadersInvokeResetStream) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  for (int i = 0; i < 100; i++) {
    request_headers.addCopy(std::to_string(i), "");
  }
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(1);
  request_encoder_->encodeHeaders(request_headers, false);
}

// Tests that max number of request headers is configurable.
TEST_P(Http2CodecImplTest, ManyRequestHeadersAccepted) {
  max_request_headers_count_ = 150;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  for (int i = 0; i < 145; i++) {
    request_headers.addCopy(std::to_string(i), "");
  }
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  request_encoder_->encodeHeaders(request_headers, false);
}

// Tests that max number of response headers is configurable.
TEST_P(Http2CodecImplTest, ManyResponseHeadersAccepted) {
  max_response_headers_count_ = 110;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"compression", "test"}};
  for (int i = 0; i < 105; i++) {
    response_headers.addCopy(std::to_string(i), "");
  }
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
}

TEST_P(Http2CodecImplTest, LargeRequestHeadersAtLimitAccepted) {
  uint32_t codec_limit_kb = 64;
  max_request_headers_kb_ = codec_limit_kb;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string key = "big";
  uint32_t head_room = 77;
  uint32_t long_string_length =
      codec_limit_kb * 1024 - request_headers.byteSize() - key.length() - head_room;
  std::string long_string = std::string(long_string_length, 'q');
  request_headers.addCopy(key, long_string);

  // The amount of data sent to the codec is not equivalent to the size of the
  // request headers that Envoy computes, as the codec limits based on the
  // entire http2 frame. The exact head room needed (76) was found through iteration.
  ASSERT_EQ(request_headers.byteSize() + head_room, codec_limit_kb * 1024);

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _));
  request_encoder_->encodeHeaders(request_headers, true);
}

TEST_P(Http2CodecImplTest, LargeRequestHeadersOverDefaultCodecLibraryLimit) {
  max_request_headers_kb_ = 66;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(65 * 1024, 'q');
  request_headers.addCopy("big", long_string);

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _)).Times(1);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  request_encoder_->encodeHeaders(request_headers, true);
}

TEST_P(Http2CodecImplTest, LargeRequestHeadersExceedPerHeaderLimit) {
  // The name-value pair max is set by NGHTTP2_HD_MAX_NV in lib/nghttp2_hd.h to 64KB, and
  // creates a per-request header limit for us in h2. Note that the nghttp2
  // calculated byte size will differ from envoy due to H2 compression and frames.

  max_request_headers_kb_ = 81;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(80 * 1024, 'q');
  request_headers.addCopy("big", long_string);

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(client_callbacks_, onGoAway());
  server_->shutdownNotice();
  server_->goAway();
  request_encoder_->encodeHeaders(request_headers, true);
}

TEST_P(Http2CodecImplTest, ManyLargeRequestHeadersUnderPerHeaderLimit) {
  max_request_headers_kb_ = 81;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(1024, 'q');
  for (int i = 0; i < 80; i++) {
    request_headers.addCopy(std::to_string(i), long_string);
  }

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _)).Times(1);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  request_encoder_->encodeHeaders(request_headers, true);
}

TEST_P(Http2CodecImplTest, LargeRequestHeadersAtMaxConfigurable) {
  // Raising the limit past this triggers some unexpected nghttp2 error.
  // Further debugging required to increase past ~96 KiB.
  max_request_headers_kb_ = 96;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(1024, 'q');
  for (int i = 0; i < 95; i++) {
    request_headers.addCopy(std::to_string(i), long_string);
  }

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _)).Times(1);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  request_encoder_->encodeHeaders(request_headers, true);
}

// Note this is Http2CodecImplTestAll not Http2CodecImplTest, to test
// compression with min and max HPACK table size.
TEST_P(Http2CodecImplTestAll, TestCodecHeaderCompression) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"compression", "test"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);

  // Sanity check to verify that state of encoders and decoders matches.
  EXPECT_EQ(nghttp2_session_get_hd_deflate_dynamic_table_size(server_->session()),
            nghttp2_session_get_hd_inflate_dynamic_table_size(client_->session()));
  EXPECT_EQ(nghttp2_session_get_hd_deflate_dynamic_table_size(client_->session()),
            nghttp2_session_get_hd_inflate_dynamic_table_size(server_->session()));

  // Verify that headers are compressed only when both client and server advertise table size > 0:
  if (client_http2settings_.hpack_table_size_ && server_http2settings_.hpack_table_size_) {
    EXPECT_NE(0, nghttp2_session_get_hd_deflate_dynamic_table_size(client_->session()));
    EXPECT_NE(0, nghttp2_session_get_hd_deflate_dynamic_table_size(server_->session()));
  } else {
    EXPECT_EQ(0, nghttp2_session_get_hd_deflate_dynamic_table_size(client_->session()));
    EXPECT_EQ(0, nghttp2_session_get_hd_deflate_dynamic_table_size(server_->session()));
  }
}

// Verify that codec detects PING flood
TEST_P(Http2CodecImplTest, PingFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Send one frame above the outbound control queue size limit
  for (uint32_t i = 0; i < Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1; ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }

  int ack_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &ack_count](Buffer::Instance& frame, bool) {
        ++ack_count;
        buffer.move(frame);
      }));

  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);
  EXPECT_EQ(ack_count, Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES);
  EXPECT_EQ(1, stats_store_.counter("http2.outbound_control_flood").value());
}

// Verify that codec allows PING flood when mitigation is disabled
TEST_P(Http2CodecImplTest, PingFloodMitigationDisabled) {
  max_outbound_control_frames_ = 2147483647;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  // Send one frame above the outbound control queue size limit
  for (uint32_t i = 0; i < Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1; ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }

  EXPECT_CALL(server_connection_, write(_, _))
      .Times(Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1);
  EXPECT_NO_THROW(client_->sendPendingFrames());
}

// Verify that outbound control frame counter decreases when send buffer is drained
TEST_P(Http2CodecImplTest, PingFloodCounterReset) {
  static const int kMaxOutboundControlFrames = 100;
  max_outbound_control_frames_ = kMaxOutboundControlFrames;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  for (int i = 0; i < kMaxOutboundControlFrames; ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }

  int ack_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &ack_count](Buffer::Instance& frame, bool) {
        ++ack_count;
        buffer.move(frame);
      }));

  // We should be 1 frame under the control frame flood mitigation threshold.
  EXPECT_NO_THROW(client_->sendPendingFrames());
  EXPECT_EQ(ack_count, kMaxOutboundControlFrames);

  // Drain kMaxOutboundFrames / 2 slices from the send buffer
  buffer.drain(buffer.length() / 2);

  // Send kMaxOutboundFrames / 2 more pings.
  for (int i = 0; i < kMaxOutboundControlFrames / 2; ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }
  // The number of outbound frames should be half of max so the connection should not be terminated.
  EXPECT_NO_THROW(client_->sendPendingFrames());

  // 1 more ping frame should overflow the outbound frame limit.
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);
}

// Verify that codec detects flood of outbound HEADER frames
TEST_P(Http2CodecImplTest, ResponseHeadersFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  for (uint32_t i = 0; i < Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES + 1; ++i) {
    EXPECT_NO_THROW(response_encoder_->encodeHeaders(response_headers, false));
  }
  // Presently flood mitigation is done only when processing downstream data
  // So we need to send stream from downstream client to trigger mitigation
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);

  EXPECT_EQ(frame_count, Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec detects flood of outbound DATA frames
TEST_P(Http2CodecImplTest, ResponseDataFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }
  // Presently flood mitigation is done only when processing downstream data
  // So we need to send stream from downstream client to trigger mitigation
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);

  EXPECT_EQ(frame_count, Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec allows outbound DATA flood when mitigation is disabled
TEST_P(Http2CodecImplTest, ResponseDataFloodMitigationDisabled) {
  max_outbound_control_frames_ = 2147483647;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  // +2 is to account for HEADERS and PING ACK, that is used to trigger mitigation
  EXPECT_CALL(server_connection_, write(_, _))
      .Times(Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES + 2);
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false)).Times(1);
  EXPECT_CALL(response_decoder_, decodeData(_, false))
      .Times(Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES);
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }
  // Presently flood mitigation is done only when processing downstream data
  // So we need to send stream from downstream client to trigger mitigation
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_NO_THROW(client_->sendPendingFrames());
}

// Verify that outbound frame counter decreases when send buffer is drained
TEST_P(Http2CodecImplTest, ResponseDataFloodCounterReset) {
  static const int kMaxOutboundFrames = 100;
  max_outbound_frames_ = kMaxOutboundFrames;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < kMaxOutboundFrames - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_EQ(frame_count, kMaxOutboundFrames);
  // Drain kMaxOutboundFrames / 2 slices from the send buffer
  buffer.drain(buffer.length() / 2);

  for (uint32_t i = 0; i < kMaxOutboundFrames / 2 + 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  // Presently flood mitigation is done only when processing downstream data
  // So we need to send a frame from downstream client to trigger mitigation
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);
}

// Verify that control frames are added to the counter of outbound frames of all types.
TEST_P(Http2CodecImplTest, PingStacksWithDataFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  request_encoder_->encodeHeaders(request_headers, false);

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }
  // Send one PING frame above the outbound queue size limit
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);

  EXPECT_EQ(frame_count, Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES);
  EXPECT_EQ(1, stats_store_.counter("http2.outbound_flood").value());
}

TEST_P(Http2CodecImplTest, PriorityFlood) {
  priorityFlood();
  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);
}

TEST_P(Http2CodecImplTest, PriorityFloodOverride) {
  max_inbound_priority_frames_per_stream_ = 2147483647;

  priorityFlood();
  EXPECT_NO_THROW(client_->sendPendingFrames());
}

TEST_P(Http2CodecImplTest, WindowUpdateFlood) {
  windowUpdateFlood();
  EXPECT_THROW(client_->sendPendingFrames(), FrameFloodException);
}

TEST_P(Http2CodecImplTest, WindowUpdateFloodOverride) {
  max_inbound_window_update_frames_per_data_frame_sent_ = 2147483647;
  windowUpdateFlood();
  EXPECT_NO_THROW(client_->sendPendingFrames());
}

TEST_P(Http2CodecImplTest, EmptyDataFlood) {
  Buffer::OwnedImpl data;
  emptyDataFlood(data);
  EXPECT_CALL(request_decoder_, decodeData(_, false));
  EXPECT_THROW(server_wrapper_.dispatch(data, *server_), FrameFloodException);
}

TEST_P(Http2CodecImplTest, EmptyDataFloodOverride) {
  max_consecutive_inbound_frames_with_empty_payload_ = 2147483647;
  Buffer::OwnedImpl data;
  emptyDataFlood(data);
  EXPECT_CALL(request_decoder_, decodeData(_, false))
      .Times(Http2Settings::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD + 1);
  EXPECT_NO_THROW(server_wrapper_.dispatch(data, *server_));
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
