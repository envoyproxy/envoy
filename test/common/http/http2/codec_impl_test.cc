#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"
#include "envoy/stats/scope.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/utility.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_impl.h"

#include "test/common/http/common.h"
#include "test/common/http/http2/http2_frame.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "codec_impl_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::EndsWith;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::StartsWith;
using std::string_literals::operator""s;

namespace Envoy {
namespace Http {
namespace Http2 {

using Http2SettingsTuple = ::testing::tuple<uint32_t, uint32_t, uint32_t, uint32_t>;
using Http2SettingsTestParam = ::testing::tuple<Http2SettingsTuple, Http2SettingsTuple>;
namespace CommonUtility = ::Envoy::Http2::Utility;

class Http2CodecImplTestFixture {
public:
  // The Http::Connection::dispatch method does not throw (any more). However unit tests in this
  // file use codecs for sending test data through mock network connections to the codec under test.
  // It is infeasible to plumb error codes returned by the dispatch() method of the codecs under
  // test, through mock connections and sending codec. As a result error returned by the dispatch
  // method of the codec under test invoked by the ConnectionWrapper is thrown as an exception. Note
  // that exception goes only through the mock network connection and sending codec, i.e. it is
  // thrown only through the test harness code. Specific exception types are to distinguish error
  // codes returned when processing requests or responses.
  // TODO(yanavlasov): modify the code to verify test expectations at the point of calling codec
  //                   under test through the ON_CALL expectations in the
  //                   setupDefaultConnectionMocks() method. This will make the exceptions below
  //                   unnecessary.
  struct ClientCodecError : public std::runtime_error {
    ClientCodecError(Http::Status&& status)
        : std::runtime_error(std::string(status.message())), status_(std::move(status)) {}
    const char* what() const noexcept override { return status_.message().data(); }
    const Http::Status status_;
  };

  struct ServerCodecError : public std::runtime_error {
    ServerCodecError(Http::Status&& status)
        : std::runtime_error(std::string(status.message())), status_(std::move(status)) {}
    const char* what() const noexcept override { return status_.message().data(); }
    const Http::Status status_;
  };

  struct ConnectionWrapper {
    Http::Status dispatch(const Buffer::Instance& data, ConnectionImpl& connection) {
      Http::Status status = Http::okStatus();
      buffer_.add(data);
      if (!dispatching_) {
        while (buffer_.length() > 0) {
          dispatching_ = true;
          status = connection.dispatch(buffer_);
          if (!status.ok()) {
            // Exit early if we hit an error status.
            return status;
          }
          dispatching_ = false;
        }
      }
      return status;
    }

    bool dispatching_{};
    Buffer::OwnedImpl buffer_;
  };

  enum SettingsTupleIndex {
    HpackTableSize = 0,
    MaxConcurrentStreams,
    InitialStreamWindowSize,
    InitialConnectionWindowSize
  };

  Http2CodecImplTestFixture() = default;
  Http2CodecImplTestFixture(Http2SettingsTuple client_settings, Http2SettingsTuple server_settings)
      : client_settings_(client_settings), server_settings_(server_settings) {
    // Make sure we explicitly test for stream flush timer creation.
    EXPECT_CALL(client_connection_.dispatcher_, createTimer_(_)).Times(0);
    EXPECT_CALL(server_connection_.dispatcher_, createTimer_(_)).Times(0);
  }
  virtual ~Http2CodecImplTestFixture() {
    client_connection_.dispatcher_.clearDeferredDeleteList();
    if (client_ != nullptr) {
      client_.reset();
      EXPECT_EQ(0, TestUtility::findGauge(client_stats_store_, "http2.streams_active")->value());
      EXPECT_EQ(0,
                TestUtility::findGauge(client_stats_store_, "http2.pending_send_bytes")->value());
    }
    server_connection_.dispatcher_.clearDeferredDeleteList();
    if (server_ != nullptr) {
      server_.reset();
      EXPECT_EQ(0, TestUtility::findGauge(server_stats_store_, "http2.streams_active")->value());
      EXPECT_EQ(0,
                TestUtility::findGauge(server_stats_store_, "http2.pending_send_bytes")->value());
    }
  }

  virtual void initialize() {
    http2OptionsFromTuple(client_http2_options_, client_settings_);
    http2OptionsFromTuple(server_http2_options_, server_settings_);
    client_ = std::make_unique<TestClientConnectionImpl>(
        client_connection_, client_callbacks_, client_stats_store_, client_http2_options_, random_,
        max_request_headers_kb_, max_response_headers_count_, ProdNghttp2SessionFactory::get());
    server_ = std::make_unique<TestServerConnectionImpl>(
        server_connection_, server_callbacks_, server_stats_store_, server_http2_options_, random_,
        max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
    request_encoder_ = &client_->newStream(response_decoder_);
    setupDefaultConnectionMocks();

    EXPECT_CALL(server_callbacks_, newStream(_, _))
        .WillRepeatedly(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
          response_encoder_ = &encoder;
          encoder.getStream().addCallbacks(server_stream_callbacks_);
          encoder.getStream().setFlushTimeout(std::chrono::milliseconds(30000));
          return request_decoder_;
        }));
  }

  void setupDefaultConnectionMocks() {
    ON_CALL(client_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          if (corrupt_metadata_frame_) {
            corruptMetadataFramePayload(data);
          }
          auto status = server_wrapper_.dispatch(data, *server_);
          if (!status.ok()) {
            throw ServerCodecError(std::move(status));
          }
        }));
    ON_CALL(server_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          auto status = client_wrapper_.dispatch(data, *client_);
          if (!status.ok()) {
            throw ClientCodecError(std::move(status));
          }
        }));
  }

  void http2OptionsFromTuple(envoy::config::core::v3::Http2ProtocolOptions& options,
                             const absl::optional<const Http2SettingsTuple>& tp) {
    options.mutable_hpack_table_size()->set_value(
        (tp.has_value()) ? ::testing::get<SettingsTupleIndex::HpackTableSize>(*tp)
                         : CommonUtility::OptionsLimits::DEFAULT_HPACK_TABLE_SIZE);
    options.mutable_max_concurrent_streams()->set_value(
        (tp.has_value()) ? ::testing::get<SettingsTupleIndex::MaxConcurrentStreams>(*tp)
                         : CommonUtility::OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS);
    options.mutable_initial_stream_window_size()->set_value(
        (tp.has_value()) ? ::testing::get<SettingsTupleIndex::InitialStreamWindowSize>(*tp)
                         : CommonUtility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
    options.mutable_initial_connection_window_size()->set_value(
        (tp.has_value()) ? ::testing::get<SettingsTupleIndex::InitialConnectionWindowSize>(*tp)
                         : CommonUtility::OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE);
    options.set_allow_metadata(allow_metadata_);
    options.mutable_override_stream_error_on_invalid_http_message()->set_value(
        stream_error_on_invalid_http_messaging_);
    options.mutable_max_outbound_frames()->set_value(max_outbound_frames_);
    options.mutable_max_outbound_control_frames()->set_value(max_outbound_control_frames_);
    options.mutable_max_consecutive_inbound_frames_with_empty_payload()->set_value(
        max_consecutive_inbound_frames_with_empty_payload_);
    options.mutable_max_inbound_priority_frames_per_stream()->set_value(
        max_inbound_priority_frames_per_stream_);
    options.mutable_max_inbound_window_update_frames_per_data_frame_sent()->set_value(
        max_inbound_window_update_frames_per_data_frame_sent_);
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

  void expectDetailsRequest(const absl::string_view details) {
    EXPECT_EQ(details, request_encoder_->getStream().responseDetails());
  }

  void expectDetailsResponse(const absl::string_view details) {
    EXPECT_EQ(details, response_encoder_->getStream().responseDetails());
  }

  absl::optional<const Http2SettingsTuple> client_settings_;
  absl::optional<const Http2SettingsTuple> server_settings_;
  bool allow_metadata_ = false;
  bool stream_error_on_invalid_http_messaging_ = false;
  Stats::TestUtil::TestStore client_stats_store_;
  envoy::config::core::v3::Http2ProtocolOptions client_http2_options_;
  NiceMock<Network::MockConnection> client_connection_;
  MockConnectionCallbacks client_callbacks_;
  std::unique_ptr<TestClientConnectionImpl> client_;
  ConnectionWrapper client_wrapper_;
  Stats::TestUtil::TestStore server_stats_store_;
  envoy::config::core::v3::Http2ProtocolOptions server_http2_options_;
  NiceMock<Random::MockRandomGenerator> random_;
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
  uint32_t max_outbound_frames_ = CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES;
  uint32_t max_outbound_control_frames_ =
      CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES;
  uint32_t max_consecutive_inbound_frames_with_empty_payload_ =
      CommonUtility::OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD;
  uint32_t max_inbound_priority_frames_per_stream_ =
      CommonUtility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM;
  uint32_t max_inbound_window_update_frames_per_data_frame_sent_ =
      CommonUtility::OptionsLimits::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT;
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_{envoy::config::core::v3::HttpProtocolOptions::ALLOW};
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
    EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

    nghttp2_priority_spec spec = {0, 10, 0};
    // HTTP/2 codec adds 1 to the number of active streams when computing PRIORITY frames limit
    constexpr uint32_t max_allowed =
        2 * CommonUtility::OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM;
    for (uint32_t i = 0; i < max_allowed + 1; ++i) {
      EXPECT_EQ(0, nghttp2_submit_priority(client_->session(), NGHTTP2_FLAG_NONE, 1, &spec));
    }
  }

  void windowUpdateFlood() {
    initialize();

    TestRequestHeaderMapImpl request_headers;
    HttpTestUtility::addDefaultHeaders(request_headers);
    EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
    EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

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
        1 + 2 * (CommonUtility::OptionsLimits::
                     DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT +
                 1);
    for (uint32_t i = 0; i < max_allowed + 1; ++i) {
      EXPECT_EQ(0, nghttp2_submit_window_update(client_->session(), NGHTTP2_FLAG_NONE, 1, 1));
    }
  }

  void emptyDataFlood(Buffer::OwnedImpl& data) {
    initialize();

    TestRequestHeaderMapImpl request_headers;
    HttpTestUtility::addDefaultHeaders(request_headers, "POST");
    EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
    EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

    // HTTP/2 codec does not send empty DATA frames with no END_STREAM flag.
    // To make this work, send raw bytes representing empty DATA frames bypassing client codec.
    Http2Frame emptyDataFrame = Http2Frame::makeEmptyDataFrame(Http2Frame::makeClientStreamId(0));
    constexpr uint32_t max_allowed =
        CommonUtility::OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD;
    for (uint32_t i = 0; i < max_allowed + 1; ++i) {
      data.add(emptyDataFrame.data(), emptyDataFrame.size());
    }
  }
};

TEST_P(Http2CodecImplTest, ShutdownNotice) {
  initialize();
  EXPECT_EQ(absl::nullopt, request_encoder_->http1StreamEncoderOptions());

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  EXPECT_CALL(client_callbacks_, onGoAway(_));
  server_->shutdownNotice();
  server_->goAway();

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
}

TEST_P(Http2CodecImplTest, ProtocolErrorForTest) {
  initialize();
  EXPECT_EQ(absl::nullopt, request_encoder_->http1StreamEncoderOptions());

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  EXPECT_CALL(client_callbacks_, onGoAway(Http::GoAwayErrorCode::Other));

  // We have to dynamic cast because protocolErrorForTest() is intentionally not on the
  // Connection API.
  ServerConnectionImpl* raw_server = dynamic_cast<ServerConnectionImpl*>(server_.get());
  ASSERT(raw_server != nullptr);
  EXPECT_EQ(StatusCode::CodecProtocolError, getStatusCode(raw_server->protocolErrorForTest()));
}

// 100 response followed by 200 results in a [decode100ContinueHeaders, decodeHeaders] sequence.
TEST_P(Http2CodecImplTest, ContinueHeaders) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
};

// nghttp2 rejects trailers with :status.
TEST_P(Http2CodecImplTest, TrailerStatus) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);

  // nghttp2 doesn't allow :status in trailers
  EXPECT_THROW(response_encoder_->encode100ContinueHeaders(continue_headers), ClientCodecError);
  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
};

// Multiple 100 responses are passed to the response encoder (who is responsible for coalescing).
TEST_P(Http2CodecImplTest, MultipleContinueHeaders) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
};

// 101/102 headers etc. are passed to the response encoder (who is responsibly for deciding to
// upgrade, ignore, etc.).
TEST_P(Http2CodecImplTest, 1xxNonContinueHeaders) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl other_headers{{":status", "102"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(other_headers, false);
};

// nghttp2 treats 101 inside an HTTP/2 stream as an invalid HTTP header field.
TEST_P(Http2CodecImplTest, Invalid101SwitchingProtocols) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl upgrade_headers{{":status", "101"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, _)).Times(0);
  EXPECT_THROW(response_encoder_->encodeHeaders(upgrade_headers, false), ClientCodecError);
  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
}

TEST_P(Http2CodecImplTest, InvalidContinueWithFin) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_THROW(response_encoder_->encodeHeaders(continue_headers, true), ClientCodecError);
  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
}

TEST_P(Http2CodecImplTest, InvalidContinueWithFinAllowed) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  // Buffer client data to avoid mock recursion causing lifetime issues.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  response_encoder_->encodeHeaders(continue_headers, true);

  // Flush pending data.
  EXPECT_CALL(request_callbacks, onResetStream(StreamResetReason::LocalReset, _));
  setupDefaultConnectionMocks();
  auto status = client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
  expectDetailsRequest("http2.violation.of.messaging.rule");
}

TEST_P(Http2CodecImplTest, CodecHasCorrectStreamErrorIfFalse) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  EXPECT_FALSE(response_encoder_->streamErrorOnInvalidHttpMessage());
}

TEST_P(Http2CodecImplTest, CodecHasCorrectStreamErrorIfTrue) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  EXPECT_TRUE(response_encoder_->streamErrorOnInvalidHttpMessage());
}

TEST_P(Http2CodecImplTest, InvalidRepeatContinue) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder_, decode100ContinueHeaders_(_));
  response_encoder_->encode100ContinueHeaders(continue_headers);

  EXPECT_THROW(response_encoder_->encodeHeaders(continue_headers, true), ClientCodecError);
  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, InvalidRepeatContinueAllowed) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

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
  auto status = client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
  expectDetailsRequest("http2.violation.of.messaging.rule");
};

TEST_P(Http2CodecImplTest, Invalid204WithContentLength) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl response_headers{{":status", "204"}, {"content-length", "3"}};
  // What follows is a hack to get headers that should span into continuation frames. The default
  // maximum frame size is 16K. We will add 3,000 headers that will take us above this size and
  // not easily compress with HPACK. (I confirmed this generates 26,468 bytes of header data
  // which should contain a continuation.)
  for (unsigned i = 1; i < 3000; i++) {
    response_headers.addCopy(std::to_string(i), std::to_string(i));
  }

  EXPECT_LOG_CONTAINS(
      "debug",
      "Invalid HTTP header field was received: frame type: 1, stream: 1, name: [content-length], "
      "value: [3]",
      EXPECT_THROW(response_encoder_->encodeHeaders(response_headers, false), ClientCodecError));
  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
};

TEST_P(Http2CodecImplTest, Invalid204WithContentLengthAllowed) {
  stream_error_on_invalid_http_messaging_ = true;
  initialize();

  MockStreamCallbacks request_callbacks;
  request_encoder_->getStream().addCallbacks(request_callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

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
  auto status = client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
  expectDetailsRequest("http2.invalid.header.field");
};

TEST_P(Http2CodecImplTest, RefusedStreamReset) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);
  EXPECT_CALL(server_stream_callbacks_,
              onResetStream(StreamResetReason::LocalRefusedStreamReset, _));
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteRefusedStreamReset, _));
  response_encoder_->getStream().resetStream(StreamResetReason::LocalRefusedStreamReset);
}

TEST_P(Http2CodecImplTest, InvalidHeadersFrame) {
  initialize();

  const auto status = request_encoder_->encodeHeaders(TestRequestHeaderMapImpl{}, true);

  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), testing::HasSubstr("missing required"));
}

TEST_P(Http2CodecImplTest, TrailingHeaders) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
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

// When having empty trailers, codec submits empty buffer and end_stream instead.
TEST_P(Http2CodecImplTest, IgnoreTrailingEmptyHeaders) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.http2_skip_encoding_empty_trailers", "true"}});

  initialize();

  Buffer::OwnedImpl empty_buffer;

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
  EXPECT_CALL(request_decoder_, decodeData(_, false));
  Buffer::OwnedImpl hello("hello");
  request_encoder_->encodeData(hello, false);
  EXPECT_CALL(request_decoder_, decodeData(BufferEqual(&empty_buffer), true));
  request_encoder_->encodeTrailers(TestRequestTrailerMapImpl{});

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder_->encodeData(world, false);
  EXPECT_CALL(response_decoder_, decodeData(BufferEqual(&empty_buffer), true));
  response_encoder_->encodeTrailers(TestResponseTrailerMapImpl{});
}

// When having empty trailers and "envoy.reloadable_features.http2_skip_encoding_empty_trailers" is
// turned off, codec submits empty trailers.
TEST_P(Http2CodecImplTest, SubmitTrailingEmptyHeaders) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.http2_skip_encoding_empty_trailers", "false"}});

  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
  EXPECT_CALL(request_decoder_, decodeData(_, false));
  Buffer::OwnedImpl hello("hello");
  request_encoder_->encodeData(hello, false);
  EXPECT_CALL(request_decoder_, decodeTrailers_(_));
  request_encoder_->encodeTrailers(TestRequestTrailerMapImpl{});

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder_->encodeData(world, false);
  EXPECT_CALL(response_decoder_, decodeTrailers_(_));
  response_encoder_->encodeTrailers(TestResponseTrailerMapImpl{});
}

TEST_P(Http2CodecImplTest, TrailingHeadersLargeClientBody) {
  initialize();

  // Buffer server data so we can make sure we don't get any window updates.
  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
  EXPECT_CALL(request_decoder_, decodeData(_, false)).Times(AtLeast(1));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  request_encoder_->encodeData(body, false);
  request_encoder_->encodeTrailers(TestRequestTrailerMapImpl{{"trailing", "header"}});

  // Flush pending data.
  setupDefaultConnectionMocks();
  EXPECT_CALL(request_decoder_, decodeTrailers_(_));
  auto status = server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);
  EXPECT_TRUE(status.ok());

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
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  EXPECT_THROW_WITH_MESSAGE(request_encoder_->encodeMetadata(metadata_map_vector), ServerCodecError,
                            "The user callback function failed");
}

// Encode response metadata while dispatching request data from the client, so
// that nghttp2 can't fill the metadata frames' payloads until dispatching
// is finished.
TEST_P(Http2CodecImplTest, EncodeMetadataWhileDispatchingTest) {
  allow_metadata_ = true;
  initialize();

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

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true)).WillOnce(InvokeWithoutArgs([&]() -> void {
    response_encoder_->encodeMetadata(metadata_map_vector);
  }));
  EXPECT_CALL(response_decoder_, decodeMetadata_(_)).Times(size);
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());
}

// Verifies that metadata is not decoded after the stream ended.
TEST_P(Http2CodecImplTest, NoMetadataEndStreamTest) {
  allow_metadata_ = true;
  initialize();

  // Generates a valid stream_id by sending a request header, and end stream.
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  const MetadataMap metadata_map = {{"header_key1", "header_value1"}};
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  // The metadata decoding will not be called after the stream has ended.
  EXPECT_CALL(request_decoder_, decodeMetadata_(_)).Times(0);
  request_encoder_->encodeMetadata(metadata_map_vector);
}

// Validate the keepalive PINGs are sent and received correctly.
TEST_P(Http2CodecImplTest, ConnectionKeepalive) {
  constexpr uint32_t interval_ms = 100;
  constexpr uint32_t timeout_ms = 200;
  client_http2_options_.mutable_connection_keepalive()->mutable_interval()->set_nanos(interval_ms *
                                                                                      1000 * 1000);
  client_http2_options_.mutable_connection_keepalive()->mutable_timeout()->set_nanos(timeout_ms *
                                                                                     1000 * 1000);
  client_http2_options_.mutable_connection_keepalive()->mutable_interval_jitter()->set_value(0);
  auto timeout_timer = new Event::MockTimer(&client_connection_.dispatcher_); /* */
  auto send_timer = new Event::MockTimer(&client_connection_.dispatcher_);
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(*send_timer, enableTimer(std::chrono::milliseconds(interval_ms), _));
  initialize();

  // Trigger sending a PING, and validate that an ACK is received based on the timeout timer
  // being disabled and the interval being re-enabled.
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(timeout_ms), _));
  EXPECT_CALL(*timeout_timer, disableTimer()); // This indicates that an ACK was received.
  EXPECT_CALL(*send_timer, enableTimer(std::chrono::milliseconds(interval_ms), _));
  send_timer->callback_();

  // Test that a timeout closes the connection.
  EXPECT_CALL(client_connection_, close(Network::ConnectionCloseType::NoFlush));
  timeout_timer->callback_();
}

// Validate that jitter is added as expected based on configuration.
TEST_P(Http2CodecImplTest, ConnectionKeepaliveJitter) {
  client_http2_options_.mutable_connection_keepalive()->mutable_interval()->set_seconds(1);
  client_http2_options_.mutable_connection_keepalive()->mutable_timeout()->set_seconds(1);
  client_http2_options_.mutable_connection_keepalive()->mutable_interval_jitter()->set_value(10);
  /*auto timeout_timer = */ new NiceMock<Event::MockTimer>(&client_connection_.dispatcher_);
  auto send_timer = new Event::MockTimer(&client_connection_.dispatcher_);

  constexpr std::chrono::milliseconds min_expected(1000);
  constexpr std::chrono::milliseconds max_expected(1099); // 1000ms + 10%
  std::chrono::milliseconds min_observed(5000);
  std::chrono::milliseconds max_observed(0);
  EXPECT_CALL(*send_timer, enableTimer(_, _))
      .WillRepeatedly(Invoke([&](const std::chrono::milliseconds& ms, const ScopeTrackedObject*) {
        EXPECT_GE(ms, std::chrono::milliseconds(1000));
        EXPECT_LE(ms, std::chrono::milliseconds(1100));
        max_observed = std::max(max_observed, ms);
        min_observed = std::min(min_observed, ms);
      }));
  initialize();

  for (uint64_t i = 0; i < 250; i++) {
    EXPECT_CALL(random_, random()).WillOnce(Return(i));
    send_timer->callback_();
  }

  EXPECT_EQ(min_observed.count(), min_expected.count());
  EXPECT_EQ(max_observed.count(), max_expected.count());
}

TEST_P(Http2CodecImplTest, DumpsStreamlessConnectionWithoutAllocatingMemory) {
  initialize();
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};

  Stats::TestUtil::MemoryTest memory_test;
  server_->dumpState(ostream, 0);

  EXPECT_EQ(memory_test.consumedBytes(), 0);
  // Check the entire dump to ensure correct formating.
  // This test might be a little brittle because of this, and hence in the other
  // dump tests we focus on the particular substring of interest.
  EXPECT_THAT(ostream.contents(), StartsWith("Http2::ConnectionImpl 0x"));
  EXPECT_THAT(
      ostream.contents(),
      HasSubstr(
          "max_headers_kb_: 60, max_headers_count_: 100, "
          "per_stream_buffer_limit_: 268435456, allow_metadata_: 0, "
          "stream_error_on_invalid_http_messaging_: 0, is_outbound_flood_monitored_control_frame_: "
          "0, skip_encoding_empty_trailers_: 1, dispatching_: 0, raised_goaway_: 0, "
          "pending_deferred_reset_: 0\n"
          "&protocol_constraints_: \n"
          "  ProtocolConstraints 0x"));
  EXPECT_THAT(
      ostream.contents(),
      EndsWith("outbound_frames_: 0, max_outbound_frames_: 10000, "
               "outbound_control_frames_: 0, max_outbound_control_frames_: 1000, "
               "consecutive_inbound_frames_with_empty_payload_: 0, "
               "max_consecutive_inbound_frames_with_empty_payload_: 1, inbound_streams_: 0, "
               "inbound_priority_frames_: 0, max_inbound_priority_frames_per_stream_: 100, "
               "inbound_window_update_frames_: 0, outbound_data_frames_: 0, "
               "max_inbound_window_update_frames_per_data_frame_sent_: 10\n"
               "Number of active streams: 0 Active Streams:\n"
               "current_slice_: null\n"));
}

TEST_P(Http2CodecImplTest, ShouldDumpActiveStreamsWithoutAllocatingMemory) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);

  // Dump server
  {
    std::array<char, 2048> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    // Check no memory allocated.
    Stats::TestUtil::MemoryTest memory_test;
    server_->dumpState(ostream, 0);
    EXPECT_EQ(memory_test.consumedBytes(), 0);

    // Check contents for active stream, trailers to encode and header map.
    EXPECT_THAT(
        ostream.contents(),
        HasSubstr(
            "Number of active streams: 1 Active Streams:\nstream: \nConnectionImpl::StreamImpl"));
    EXPECT_THAT(ostream.contents(),
                HasSubstr("pending_trailers_to_encode_:   null\n"
                          "  absl::get<RequestHeaderMapPtr>(headers_or_trailers_): \n"
                          "    ':scheme', 'http'\n"
                          "    ':method', 'GET'\n"
                          "    ':authority', 'host'\n"
                          "    ':path', '/'\n"
                          "current_slice_: null"));
  }

  // Dump client
  {
    std::array<char, 2048> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    // Check no memory allocated.
    Stats::TestUtil::MemoryTest memory_test;
    client_->dumpState(ostream, 0);
    EXPECT_EQ(memory_test.consumedBytes(), 0);

    // Check contents for active stream, trailers to encode and header map.
    EXPECT_THAT(ostream.contents(), HasSubstr("Number of active streams: 1 Active Streams:\n"
                                              "stream: \n"
                                              "ConnectionImpl::StreamImpl"));
    EXPECT_THAT(ostream.contents(),
                HasSubstr("pending_trailers_to_encode_:   null\n"
                          "  absl::get<ResponseHeaderMapPtr>(headers_or_trailers_): \n"
                          "    ':status', '200'\n"
                          "current_slice_: null"));
  }
}

TEST_P(Http2CodecImplTest, ShouldDumpCurrentSliceWithoutAllocatingMemory) {
  initialize();
  std::array<char, 2048> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  // Send headers
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  // Send data payload, dump buffer as decoding data
  EXPECT_CALL(request_decoder_, decodeData(_, false)).WillOnce(Invoke([&](Buffer::Instance&, bool) {
    // dumpState here while we had a current slice of data. No Memory should be
    // allocated.
    Stats::TestUtil::MemoryTest memory_test;
    server_->dumpState(ostream, 0);
    EXPECT_EQ(memory_test.consumedBytes(), 0);
  }));
  Buffer::OwnedImpl hello("hello envoy");
  request_encoder_->encodeData(hello, false);

  // Check contents for the current slice information
  EXPECT_THAT(ostream.contents(),
              HasSubstr("current slice length: 20 contents: \"\0\0\\v\0\0\0\0\0\x1hello envoy"s));
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
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
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
  auto status = server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);
  EXPECT_TRUE(status.ok());
}

TEST_P(Http2CodecImplDeferredResetTest, DeferredResetServer) {
  initialize();

  InSequence s;

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  // In this case we do the same thing as DeferredResetClient but on the server side.
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { client_wrapper_.buffer_.add(data); }));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark()).Times(AnyNumber());
  auto flush_timer = new Event::MockTimer(&server_connection_.dispatcher_);
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(30000), _));
  response_encoder_->encodeData(body, true);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::LocalReset, _));
  EXPECT_CALL(*flush_timer, disableTimer());
  response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);

  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  EXPECT_CALL(client_stream_callbacks, onResetStream(StreamResetReason::RemoteReset, _));
  setupDefaultConnectionMocks();
  auto status = client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);
  EXPECT_TRUE(status.ok());
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
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  // Force the server stream to be read disabled. This will cause it to stop sending window
  // updates to the client.
  server_->getStream(1)->readDisable(true);
  EXPECT_EQ(1, TestUtility::findGauge(client_stats_store_, "http2.streams_active")->value());
  EXPECT_EQ(1, TestUtility::findGauge(server_stats_store_, "http2.streams_active")->value());

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
  EXPECT_EQ(initial_stream_window,
            TestUtility::findGauge(client_stats_store_, "http2.pending_send_bytes")->value());
  EXPECT_EQ(initial_stream_window, server_->getStream(1)->unconsumed_bytes_);

  // If we go over the limit, the stream callbacks should fire.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  Buffer::OwnedImpl last_byte("!");
  request_encoder_->encodeData(last_byte, false);
  EXPECT_EQ(initial_stream_window + 1, client_->getStream(1)->pending_send_data_.length());
  EXPECT_EQ(initial_stream_window + 1,
            TestUtility::findGauge(client_stats_store_, "http2.pending_send_bytes")->value());

  // Now create a second stream on the connection.
  MockResponseDecoder response_decoder2;
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
  EXPECT_TRUE(request_encoder2->encodeHeaders(request_headers, false).ok());

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
  EXPECT_EQ(0, TestUtility::findGauge(client_stats_store_, "http2.pending_send_bytes")->value());
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
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  // Set artificially small watermarks to make the recv buffer easy to overrun. In production,
  // the recv buffer can be overrun by a client which negotiates a larger
  // SETTINGS_MAX_FRAME_SIZE but there's no current easy way to tweak that in
  // envoy (without sending raw HTTP/2 frames) so we lower the buffer limit instead.
  server_->getStream(1)->setWriteBufferWatermarks(10, 20);

  EXPECT_CALL(request_decoder_, decodeData(_, false));
  Buffer::OwnedImpl data(std::string(40, 'a'));
  request_encoder_->encodeData(data, false);
}

// Verify that we create and disable the stream flush timer when trailers follow a stream that
// does not have enough window.
TEST_P(Http2CodecImplFlowControlTest, TrailingHeadersLargeServerBody) {
  initialize();

  InSequence s;
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  auto flush_timer = new Event::MockTimer(&server_connection_.dispatcher_);
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(30000), _));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  response_encoder_->encodeData(body, false);
  response_encoder_->encodeTrailers(TestResponseTrailerMapImpl{{"trailing", "header"}});

  // Send window updates from the client.
  setupDefaultConnectionMocks();
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  EXPECT_CALL(response_decoder_, decodeTrailers_(_));
  EXPECT_CALL(*flush_timer, disableTimer());
  auto status = server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0, server_stats_store_.counter("http2.tx_flush_timeout").value());
}

// Verify that we create and handle the stream flush timeout when trailers follow a stream that
// does not have enough window.
TEST_P(Http2CodecImplFlowControlTest, TrailingHeadersLargeServerBodyFlushTimeout) {
  initialize();

  InSequence s;
  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(server_stream_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  auto flush_timer = new Event::MockTimer(&server_connection_.dispatcher_);
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(30000), _));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  response_encoder_->encodeData(body, false);
  response_encoder_->encodeTrailers(TestResponseTrailerMapImpl{{"trailing", "header"}});

  // Invoke a stream flush timeout. Make sure we don't get a reset locally for higher layers but
  // we do get a reset on the client.
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  EXPECT_CALL(client_stream_callbacks, onResetStream(StreamResetReason::RemoteReset, _));
  flush_timer->invokeCallback();
  EXPECT_EQ(1, server_stats_store_.counter("http2.tx_flush_timeout").value());
}

// Verify that we create and handle the stream flush timeout when there is a large body that
// does not have enough window.
TEST_P(Http2CodecImplFlowControlTest, LargeServerBodyFlushTimeout) {
  initialize();

  InSequence s;
  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  auto flush_timer = new Event::MockTimer(&server_connection_.dispatcher_);
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(30000), _));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  response_encoder_->encodeData(body, true);

  // Invoke a stream flush timeout. Make sure we don't get a reset locally for higher layers but
  // we do get a reset on the client.
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  EXPECT_CALL(client_stream_callbacks, onResetStream(StreamResetReason::RemoteReset, _));
  flush_timer->invokeCallback();
  EXPECT_EQ(1, server_stats_store_.counter("http2.tx_flush_timeout").value());
}

// Verify that when an incoming protocol error races with a stream flush timeout we correctly
// disable the flush timeout and do not attempt to reset the stream.
TEST_P(Http2CodecImplFlowControlTest, LargeServerBodyFlushTimeoutAfterGoaway) {
  initialize();

  InSequence s;
  MockStreamCallbacks client_stream_callbacks;
  request_encoder_->getStream().addCallbacks(client_stream_callbacks);
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  ON_CALL(client_connection_, write(_, _))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data, bool) -> void { server_wrapper_.buffer_.add(data); }));
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  response_encoder_->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder_, decodeData(_, false)).Times(AtLeast(1));
  auto flush_timer = new Event::MockTimer(&server_connection_.dispatcher_);
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(30000), _));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  response_encoder_->encodeData(body, true);

  // Force a protocol error.
  Buffer::OwnedImpl garbage_data("this should cause a protocol error");
  EXPECT_CALL(client_callbacks_, onGoAway(_));
  EXPECT_CALL(*flush_timer, disableTimer());
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  auto status = server_wrapper_.dispatch(garbage_data, *server_);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(0, server_stats_store_.counter("http2.tx_flush_timeout").value());
}

// Verify detection of downstream outbound frame queue by the WINDOW_UPDATE frames
// sent when codec resumes reading.
TEST_P(Http2CodecImplFlowControlTest, WindowUpdateOnReadResumingFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

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
  Buffer::OwnedImpl long_data(std::string(initial_stream_window / 2, 'a'));
  request_encoder_->encodeData(long_data, false);

  EXPECT_EQ(initial_stream_window / 2, server_->getStream(1)->unconsumed_bytes_);

  // pre-fill downstream outbound frame queue
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above and pre-fill outbound queue with 1 byte DATA frames
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 2; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_FALSE(violation_callback->enabled_);

  // Now unblock the server's stream. This will cause the bytes to be consumed, 2 flow control
  // updates to be sent, and overflow outbound frame queue.
  server_->getStream(1)->readDisable(false);

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify detection of outbound queue flooding by the RST_STREAM frame sent by the pending flush
// timeout.
TEST_P(Http2CodecImplFlowControlTest, RstStreamOnPendingFlushTimeoutFlood) {
  // This test sets initial stream window to 65535 bytes.
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above and pre-fill outbound queue with 6 byte DATA frames
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 2; ++i) {
    Buffer::OwnedImpl data(std::string(6, '0'));
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  // client stream windows should have 5535 bytes left and the next frame should overflow it.
  // nghttp2 sends 1 DATA frame for the remainder of the client window and it should make
  // outbound frame queue 1 away from overflow.
  auto flush_timer = new Event::MockTimer(&server_connection_.dispatcher_);
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(30000), _));
  Buffer::OwnedImpl large_body(std::string(6 * 1024, '1'));
  response_encoder_->encodeData(large_body, true);

  EXPECT_FALSE(violation_callback->enabled_);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _));

  // Pending flush timeout causes RST_STREAM to be sent and overflow the outbound frame queue.
  flush_timer->invokeCallback();

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(1, server_stats_store_.counter("http2.tx_flush_timeout").value());
  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

TEST_P(Http2CodecImplTest, WatermarkUnderEndStream) {
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  http2OptionsFromTuple(client_http2_options_, ::testing::get<0>(GetParam()));
  http2OptionsFromTuple(server_http2_options_, ::testing::get<1>(GetParam()));
  client_ = std::make_unique<TestClientConnectionImpl>(
      client_connection_, client_callbacks_, client_stats_store_, client_http2_options_, random_,
      max_request_headers_kb_, max_response_headers_count_, ProdNghttp2SessionFactory::get());
  server_ = std::make_unique<TestServerConnectionImpl>(
      server_connection_, server_callbacks_, server_stats_store_, server_http2_options_, random_,
      max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
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
    EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());
  }
}

#define HTTP2SETTINGS_SMALL_WINDOW_COMBINE                                                         \
  ::testing::Combine(                                                                              \
      ::testing::Values(CommonUtility::OptionsLimits::DEFAULT_HPACK_TABLE_SIZE),                   \
      ::testing::Values(CommonUtility::OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS),             \
      ::testing::Values(CommonUtility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE),             \
      ::testing::Values(CommonUtility::OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE))

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
  ::testing::Combine(                                                                              \
      ::testing::Values(CommonUtility::OptionsLimits::DEFAULT_HPACK_TABLE_SIZE),                   \
      ::testing::Values(CommonUtility::OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS),             \
      ::testing::Values(CommonUtility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE),         \
      ::testing::Values(CommonUtility::OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE))

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
      ::testing::Values(CommonUtility::OptionsLimits::MIN_HPACK_TABLE_SIZE,                        \
                        CommonUtility::OptionsLimits::MAX_HPACK_TABLE_SIZE),                       \
      ::testing::Values(CommonUtility::OptionsLimits::MIN_MAX_CONCURRENT_STREAMS,                  \
                        CommonUtility::OptionsLimits::MAX_MAX_CONCURRENT_STREAMS),                 \
      ::testing::Values(CommonUtility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE,              \
                        CommonUtility::OptionsLimits::MAX_INITIAL_STREAM_WINDOW_SIZE),             \
      ::testing::Values(CommonUtility::OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE,          \
                        CommonUtility::OptionsLimits::MAX_INITIAL_CONNECTION_WINDOW_SIZE))

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

MATCHER_P(HasValue, m, "") {
  if (!arg.has_value()) {
    *result_listener << "does not contain a value";
    return false;
  }
  const auto& value = arg.value();
  return ExplainMatchResult(m, value, result_listener);
};

class Http2CustomSettingsTestBase : public Http2CodecImplTestFixture {
public:
  struct SettingsParameter {
    uint16_t identifier;
    uint32_t value;
  };

  Http2CustomSettingsTestBase(Http2SettingsTuple client_settings,
                              Http2SettingsTuple server_settings, bool validate_client)
      : Http2CodecImplTestFixture(client_settings, server_settings),
        validate_client_(validate_client) {}

  ~Http2CustomSettingsTestBase() override = default;

  // Sets the custom settings parameters specified by |parameters| in the |options| proto.
  void setHttp2CustomSettingsParameters(envoy::config::core::v3::Http2ProtocolOptions& options,
                                        std::vector<SettingsParameter> parameters) {
    for (const auto& parameter : parameters) {
      envoy::config::core::v3::Http2ProtocolOptions::SettingsParameter* custom_param =
          options.mutable_custom_settings_parameters()->Add();
      custom_param->mutable_identifier()->set_value(parameter.identifier);
      custom_param->mutable_value()->set_value(parameter.value);
    }
  }

  // Returns the Http2ProtocolOptions proto which specifies the settings parameters to be sent to
  // the endpoint being validated.
  envoy::config::core::v3::Http2ProtocolOptions& getCustomOptions() {
    return validate_client_ ? server_http2_options_ : client_http2_options_;
  }

  // Returns the endpoint being validated.
  const TestCodecSettingsProvider& getSettingsProvider() {
    if (validate_client_) {
      return *client_;
    }
    return *server_;
  }

  // Returns the settings tuple which specifies a subset of the settings parameters to be sent to
  // the endpoint being validated.
  const Http2SettingsTuple& getSettingsTuple() {
    ASSERT(client_settings_.has_value() && server_settings_.has_value());
    return validate_client_ ? *server_settings_ : *client_settings_;
  }

protected:
  bool validate_client_{false};
};

class Http2CustomSettingsTest
    : public Http2CustomSettingsTestBase,
      public ::testing::TestWithParam<
          ::testing::tuple<Http2SettingsTuple, Http2SettingsTuple, bool>> {
public:
  Http2CustomSettingsTest()
      : Http2CustomSettingsTestBase(::testing::get<0>(GetParam()), ::testing::get<1>(GetParam()),
                                    ::testing::get<2>(GetParam())) {}
};
INSTANTIATE_TEST_SUITE_P(Http2CodecImplTestEdgeSettings, Http2CustomSettingsTest,
                         ::testing::Combine(HTTP2SETTINGS_DEFAULT_COMBINE,
                                            HTTP2SETTINGS_DEFAULT_COMBINE, ::testing::Bool()));

// Validates that custom parameters (those which are not explicitly named in the
// envoy::config::core::v3::Http2ProtocolOptions proto) are properly sent and processed by
// client and server connections.
TEST_P(Http2CustomSettingsTest, UserDefinedSettings) {
  std::vector<SettingsParameter> custom_parameters{{0x10, 10}, {0x11, 20}};
  setHttp2CustomSettingsParameters(getCustomOptions(), custom_parameters);
  initialize();
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
  uint32_t hpack_table_size =
      ::testing::get<SettingsTupleIndex::HpackTableSize>(getSettingsTuple());
  if (hpack_table_size != NGHTTP2_DEFAULT_HEADER_TABLE_SIZE) {
    EXPECT_THAT(
        getSettingsProvider().getRemoteSettingsParameterValue(NGHTTP2_SETTINGS_HEADER_TABLE_SIZE),
        HasValue(hpack_table_size));
  }
  uint32_t max_concurrent_streams =
      ::testing::get<SettingsTupleIndex::MaxConcurrentStreams>(getSettingsTuple());
  if (max_concurrent_streams != NGHTTP2_INITIAL_MAX_CONCURRENT_STREAMS) {
    EXPECT_THAT(getSettingsProvider().getRemoteSettingsParameterValue(
                    NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS),
                HasValue(max_concurrent_streams));
  }
  uint32_t initial_stream_window_size =
      ::testing::get<SettingsTupleIndex::InitialStreamWindowSize>(getSettingsTuple());
  if (max_concurrent_streams != NGHTTP2_INITIAL_WINDOW_SIZE) {
    EXPECT_THAT(
        getSettingsProvider().getRemoteSettingsParameterValue(NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE),
        HasValue(initial_stream_window_size));
  }
  // Validate that custom parameters are received by the endpoint (client or server) under
  // test.
  for (const auto& parameter : custom_parameters) {
    EXPECT_THAT(getSettingsProvider().getRemoteSettingsParameterValue(parameter.identifier),
                HasValue(parameter.value));
  }
}

// Tests request headers whose size is larger than the default limit of 60K.
TEST_P(Http2CodecImplTest, LargeRequestHeadersInvokeResetStream) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(63 * 1024, 'q');
  request_headers.addCopy("big", long_string);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
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
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
}

// Tests request headers with name containing underscore are dropped when the option is set to drop
// header.
TEST_P(Http2CodecImplTest, HeaderNameWithUnderscoreAreDropped) {
  headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestRequestHeaderMapImpl expected_headers(request_headers);
  request_headers.addCopy("bad_header", "something");
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), _));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
  EXPECT_EQ(1, server_stats_store_.counter("http2.dropped_headers_with_underscores").value());
}

// Tests that request with header names containing underscore are rejected when the option is set to
// reject request.
TEST_P(Http2CodecImplTest, HeaderNameWithUnderscoreAreRejectedByDefault) {
  headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::REJECT_REQUEST;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  request_headers.addCopy("bad_header", "something");
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
  EXPECT_EQ(
      1,
      server_stats_store_.counter("http2.requests_rejected_with_underscores_in_headers").value());
}

// Tests request headers with name containing underscore are allowed when the option is set to
// allow.
TEST_P(Http2CodecImplTest, HeaderNameWithUnderscoreAllowed) {
  headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::ALLOW;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  request_headers.addCopy("bad_header", "something");
  TestRequestHeaderMapImpl expected_headers(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
  EXPECT_EQ(0, server_stats_store_.counter("http2.dropped_headers_with_underscores").value());
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
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
}

// Tests stream reset when the number of request headers exceeds the default maximum of 100.
TEST_P(Http2CodecImplTest, ManyRequestHeadersInvokeResetStream) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  for (int i = 0; i < 100; i++) {
    request_headers.addCopy(std::to_string(i), "");
  }
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
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
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());
}

// Tests that max number of response headers is configurable.
TEST_P(Http2CodecImplTest, ManyResponseHeadersAccepted) {
  max_response_headers_count_ = 110;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());
}

TEST_P(Http2CodecImplTest, LargeRequestHeadersOverDefaultCodecLibraryLimit) {
  max_request_headers_kb_ = 66;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  std::string long_string = std::string(65 * 1024, 'q');
  request_headers.addCopy("big", long_string);

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());
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
  EXPECT_CALL(client_callbacks_, onGoAway(_));
  server_->shutdownNotice();
  server_->goAway();
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());
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

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());
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

  EXPECT_CALL(request_decoder_, decodeHeaders_(_, _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(0);
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());
}

// Note this is Http2CodecImplTestAll not Http2CodecImplTest, to test
// compression with min and max HPACK table size.
TEST_P(Http2CodecImplTestAll, TestCodecHeaderCompression) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, true).ok());

  TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"compression", "test"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);

  // Sanity check to verify that state of encoders and decoders matches.
  EXPECT_EQ(nghttp2_session_get_hd_deflate_dynamic_table_size(server_->session()),
            nghttp2_session_get_hd_inflate_dynamic_table_size(client_->session()));
  EXPECT_EQ(nghttp2_session_get_hd_deflate_dynamic_table_size(client_->session()),
            nghttp2_session_get_hd_inflate_dynamic_table_size(server_->session()));

  // Verify that headers are compressed only when both client and server advertise table size
  // > 0:
  if (client_http2_options_.hpack_table_size().value() &&
      server_http2_options_.hpack_table_size().value()) {
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
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  // Send one frame above the outbound control queue size limit
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1;
       ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }

  int ack_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &ack_count](Buffer::Instance& frame, bool) {
        ++ack_count;
        buffer.move(frame);
      }));

  EXPECT_THROW_WITH_MESSAGE(client_->sendPendingFrames().IgnoreError(), ServerCodecError,
                            "Too many control frames in the outbound queue.");
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_control_flood").value());
}

// Verify that codec allows PING flood when mitigation is disabled
TEST_P(Http2CodecImplTest, PingFloodMitigationDisabled) {
  max_outbound_control_frames_ = 2147483647;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  // Send one frame above the outbound control queue size limit
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1;
       ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }

  EXPECT_CALL(server_connection_, write(_, _))
      .Times(CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1);
  EXPECT_NO_THROW(client_->sendPendingFrames().IgnoreError());
}

// Verify that outbound control frame counter decreases when send buffer is drained
TEST_P(Http2CodecImplTest, PingFloodCounterReset) {
  // Ping frames are 17 bytes each so 240 full frames and a partial frame fit in the current min
  // size for buffer slices. Setting the limit to 2x+1 the number that fits in a single slice allows
  // the logic below that verifies drain and overflow thresholds.
  static const int kMaxOutboundControlFrames = 481;
  max_outbound_control_frames_ = kMaxOutboundControlFrames;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  EXPECT_NO_THROW(client_->sendPendingFrames().IgnoreError());
  EXPECT_EQ(ack_count, kMaxOutboundControlFrames);

  // Drain floor(kMaxOutboundFrames / 2) slices from the send buffer
  buffer.drain(buffer.length() / 2);

  // Send floor(kMaxOutboundFrames / 2) more pings.
  for (int i = 0; i < kMaxOutboundControlFrames / 2; ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }
  // The number of outbound frames should be half of max so the connection should not be
  // terminated.
  EXPECT_NO_THROW(client_->sendPendingFrames().IgnoreError());
  EXPECT_EQ(ack_count, kMaxOutboundControlFrames + kMaxOutboundControlFrames / 2);

  // 1 more ping frame should overflow the outbound frame limit.
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW_WITH_MESSAGE(client_->sendPendingFrames().IgnoreError(), ServerCodecError,
                            "Too many control frames in the outbound queue.");
}

// Verify that codec detects flood of outbound HEADER frames
TEST_P(Http2CodecImplTest, ResponseHeadersFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1; ++i) {
    EXPECT_NO_THROW(response_encoder_->encodeHeaders(response_headers, false));
  }

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec detects flood of outbound DATA frames
TEST_P(Http2CodecImplTest, ResponseDataFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec allows outbound DATA flood when mitigation is disabled
TEST_P(Http2CodecImplTest, ResponseDataFloodMitigationDisabled) {
  max_outbound_control_frames_ = 2147483647;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  // +2 is to account for HEADERS and PING ACK, that is used to trigger mitigation
  EXPECT_CALL(server_connection_, write(_, _))
      .Times(CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 2);
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false));
  EXPECT_CALL(response_decoder_, decodeData(_, false))
      .Times(CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES);
  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }
  // Presently flood mitigation is done only when processing downstream data
  // So we need to send stream from downstream client to trigger mitigation
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_NO_THROW(client_->sendPendingFrames().IgnoreError());
}

// Verify that outbound frame counter decreases when send buffer is drained
TEST_P(Http2CodecImplTest, ResponseDataFloodCounterReset) {
  static const int kMaxOutboundFrames = 100;
  max_outbound_frames_ = kMaxOutboundFrames;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  for (uint32_t i = 0; i < kMaxOutboundFrames / 2 + 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();
}

// Verify that control frames are added to the counter of outbound frames of all types.
TEST_P(Http2CodecImplTest, PingStacksWithDataFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

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
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }
  // Send one PING frame above the outbound queue size limit
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW_WITH_MESSAGE(client_->sendPendingFrames().IgnoreError(), ServerCodecError,
                            "Too many frames in the outbound queue.");

  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec detects flood of outbound trailers
TEST_P(Http2CodecImplTest, ResponseTrailersFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_FALSE(violation_callback->enabled_);
  EXPECT_NO_THROW(response_encoder_->encodeTrailers(TestResponseTrailerMapImpl{{"foo", "bar"}}));

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec detects flood of outbound METADATA frames
TEST_P(Http2CodecImplTest, MetadataFlood) {
  allow_metadata_ = true;
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_FALSE(violation_callback->enabled_);

  MetadataMapVector metadata_map_vector;
  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
      {"header_key2", "header_value2"},
  };
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  response_encoder_->encodeMetadata(metadata_map_vector);

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

TEST_P(Http2CodecImplTest, PriorityFlood) {
  priorityFlood();
  EXPECT_THROW_WITH_MESSAGE(client_->sendPendingFrames().IgnoreError(), ServerCodecError,
                            "Too many PRIORITY frames");
}

TEST_P(Http2CodecImplTest, PriorityFloodOverride) {
  max_inbound_priority_frames_per_stream_ = 2147483647;

  priorityFlood();
  EXPECT_NO_THROW(client_->sendPendingFrames().IgnoreError());
}

TEST_P(Http2CodecImplTest, WindowUpdateFlood) {
  windowUpdateFlood();
  EXPECT_THROW_WITH_MESSAGE(client_->sendPendingFrames().IgnoreError(), ServerCodecError,
                            "Too many WINDOW_UPDATE frames");
}

TEST_P(Http2CodecImplTest, WindowUpdateFloodOverride) {
  max_inbound_window_update_frames_per_data_frame_sent_ = 2147483647;
  windowUpdateFlood();
  EXPECT_NO_THROW(client_->sendPendingFrames().IgnoreError());
}

TEST_P(Http2CodecImplTest, EmptyDataFlood) {
  Buffer::OwnedImpl data;
  emptyDataFlood(data);
  EXPECT_CALL(request_decoder_, decodeData(_, false));
  auto status = server_wrapper_.dispatch(data, *server_);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(isInboundFramesWithEmptyPayloadError(status));
  EXPECT_EQ("Too many consecutive frames with an empty payload", status.message());
}

TEST_P(Http2CodecImplTest, EmptyDataFloodOverride) {
  max_consecutive_inbound_frames_with_empty_payload_ = 2147483647;
  Buffer::OwnedImpl data;
  emptyDataFlood(data);
  EXPECT_CALL(request_decoder_, decodeData(_, false))
      .Times(
          CommonUtility::OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD +
          1);
  auto status = server_wrapper_.dispatch(data, *server_);
  EXPECT_TRUE(status.ok());
}

// Verify that codec detects flood of outbound frames caused by goAway() method
TEST_P(Http2CodecImplTest, GoAwayCausesOutboundFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_FALSE(violation_callback->enabled_);

  server_->goAway();

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec detects flood of outbound frames caused by shutdownNotice() method
TEST_P(Http2CodecImplTest, ShudowNoticeCausesOutboundFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_FALSE(violation_callback->enabled_);

  server_->shutdownNotice();

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec detects flood of outbound PING frames caused by the keep alive timer
TEST_P(Http2CodecImplTest, KeepAliveCausesOutboundFlood) {
  // set-up server to send PING frames
  constexpr uint32_t interval_ms = 100;
  constexpr uint32_t timeout_ms = 200;
  server_http2_options_.mutable_connection_keepalive()->mutable_interval()->set_nanos(interval_ms *
                                                                                      1000 * 1000);
  server_http2_options_.mutable_connection_keepalive()->mutable_timeout()->set_nanos(timeout_ms *
                                                                                     1000 * 1000);
  server_http2_options_.mutable_connection_keepalive()->mutable_interval_jitter()->set_value(0);
  auto timeout_timer = new Event::MockTimer(&server_connection_.dispatcher_); /* */
  auto send_timer = new Event::MockTimer(&server_connection_.dispatcher_);
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(*send_timer, enableTimer(std::chrono::milliseconds(interval_ms), _));

  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Pre-fill outbound frame queue 1 away from overflow (account for the single HEADERS frame above)
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_FALSE(violation_callback->enabled_);

  // Trigger sending a PING, which should overflow the outbound frame queue and cause
  // client to be disconnected
  send_timer->callback_();

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// Verify that codec detects flood of RST_STREAM frame caused by resetStream() method
TEST_P(Http2CodecImplTest, ResetStreamCausesOutboundFlood) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  int frame_count = 0;
  Buffer::OwnedImpl buffer;
  ON_CALL(server_connection_, write(_, _))
      .WillByDefault(Invoke([&buffer, &frame_count](Buffer::Instance& frame, bool) {
        ++frame_count;
        buffer.move(frame);
      }));

  auto* violation_callback =
      new NiceMock<Event::MockSchedulableCallback>(&server_connection_.dispatcher_);

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  response_encoder_->encodeHeaders(response_headers, false);
  // Account for the single HEADERS frame above
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }

  EXPECT_FALSE(violation_callback->enabled_);
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::RemoteReset, _));

  server_->getStream(1)->resetStream(StreamResetReason::RemoteReset);

  EXPECT_TRUE(violation_callback->enabled_);
  EXPECT_CALL(server_connection_, close(Envoy::Network::ConnectionCloseType::NoFlush));
  violation_callback->invokeCallback();

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

// CONNECT without upgrade type gets tagged with "bytestream"
TEST_P(Http2CodecImplTest, ConnectTest) {
  client_http2_options_.set_allow_connect(true);
  server_http2_options_.set_allow_connect(true);
  initialize();
  MockStreamCallbacks callbacks;
  request_encoder_->getStream().addCallbacks(callbacks);

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  request_headers.setReferenceKey(Headers::get().Method, Http::Headers::get().MethodValues.Connect);
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  expected_headers.setReferenceKey(Headers::get().Method,
                                   Http::Headers::get().MethodValues.Connect);
  expected_headers.setReferenceKey(Headers::get().Protocol, "bytestream");
  EXPECT_CALL(request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_TRUE(request_encoder_->encodeHeaders(request_headers, false).ok());

  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::ConnectError, _));
  EXPECT_CALL(server_stream_callbacks_, onResetStream(StreamResetReason::ConnectError, _));
  response_encoder_->getStream().resetStream(StreamResetReason::ConnectError);
}

class TestNghttp2SessionFactory;

// Test client for H/2 METADATA frame edge cases.
class MetadataTestClientConnectionImpl : public TestClientConnectionImpl {
public:
  MetadataTestClientConnectionImpl(
      Network::Connection& connection, Http::ConnectionCallbacks& callbacks, Stats::Scope& scope,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      Random::RandomGenerator& random, uint32_t max_request_headers_kb,
      uint32_t max_request_headers_count, Nghttp2SessionFactory& http2_session_factory)
      : TestClientConnectionImpl(connection, callbacks, scope, http2_options, random,
                                 max_request_headers_kb, max_request_headers_count,
                                 http2_session_factory) {}

  // Overrides TestClientConnectionImpl::submitMetadata().
  bool submitMetadata(const MetadataMapVector& metadata_map_vector, int32_t stream_id) override {
    // Creates metadata payload.
    encoder_.createPayload(metadata_map_vector);
    for (uint8_t flags : encoder_.payloadFrameFlagBytes()) {
      int result = nghttp2_submit_extension(session(), ::Envoy::Http::METADATA_FRAME_TYPE, flags,
                                            stream_id, nullptr);
      if (result != 0) {
        return false;
      }
    }
    // Triggers nghttp2 to populate the payloads of the METADATA frames.
    int result = nghttp2_session_send(session());
    return result == 0;
  }

protected:
  friend class TestNghttp2SessionFactory;

  MetadataEncoder encoder_;
};

class TestNghttp2SessionFactory : public Nghttp2SessionFactory {
public:
  ~TestNghttp2SessionFactory() override {
    nghttp2_session_callbacks_del(callbacks_);
    nghttp2_option_del(options_);
  }

  nghttp2_session* create(const nghttp2_session_callbacks*, ConnectionImpl* connection,
                          const nghttp2_option*) override {
    // Only need to provide callbacks required to send METADATA frames.
    nghttp2_session_callbacks_new(&callbacks_);
    nghttp2_session_callbacks_set_pack_extension_callback(
        callbacks_,
        [](nghttp2_session*, uint8_t* data, size_t length, const nghttp2_frame*,
           void* user_data) -> ssize_t {
          // Double cast required due to multiple inheritance.
          return static_cast<MetadataTestClientConnectionImpl*>(
                     static_cast<ConnectionImpl*>(user_data))
              ->encoder_.packNextFramePayload(data, length);
        });
    nghttp2_session_callbacks_set_send_callback(
        callbacks_,
        [](nghttp2_session*, const uint8_t* data, size_t length, int, void* user_data) -> ssize_t {
          // Cast down to MetadataTestClientConnectionImpl to leverage friendship.
          return static_cast<MetadataTestClientConnectionImpl*>(
                     static_cast<ConnectionImpl*>(user_data))
              ->onSend(data, length);
        });
    nghttp2_option_new(&options_);
    nghttp2_option_set_user_recv_extension_type(options_, METADATA_FRAME_TYPE);
    nghttp2_session* session;
    nghttp2_session_client_new2(&session, callbacks_, connection, options_);
    return session;
  }

  void init(nghttp2_session*, ConnectionImpl*,
            const envoy::config::core::v3::Http2ProtocolOptions&) override {}

private:
  nghttp2_session_callbacks* callbacks_;
  nghttp2_option* options_;
};

class Http2CodecMetadataTest : public Http2CodecImplTestFixture, public ::testing::Test {
public:
  Http2CodecMetadataTest() = default;

protected:
  void initialize() override {
    allow_metadata_ = true;
    http2OptionsFromTuple(client_http2_options_, client_settings_);
    http2OptionsFromTuple(server_http2_options_, server_settings_);
    client_ = std::make_unique<MetadataTestClientConnectionImpl>(
        client_connection_, client_callbacks_, client_stats_store_, client_http2_options_, random_,
        max_request_headers_kb_, max_response_headers_count_, http2_session_factory_);
    server_ = std::make_unique<TestServerConnectionImpl>(
        server_connection_, server_callbacks_, server_stats_store_, server_http2_options_, random_,
        max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
    ON_CALL(client_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          ASSERT_TRUE(server_wrapper_.dispatch(data, *server_).ok());
        }));
    ON_CALL(server_connection_, write(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
          ASSERT_TRUE(client_wrapper_.dispatch(data, *client_).ok());
        }));
  }

private:
  TestNghttp2SessionFactory http2_session_factory_;
};

// Validates noop handling of METADATA frames without a known stream ID.
// This is required per RFC 7540, section 5.1.1, which states that stream ID = 0 can be used for
// "connection control" messages, and per the H2 METADATA spec (source/docs/h2_metadata.md), which
// states that these frames can be received prior to the headers.
TEST_F(Http2CodecMetadataTest, UnknownStreamId) {
  initialize();
  MetadataMap metadata_map = {{"key", "value"}};
  MetadataMapVector metadata_vector;
  metadata_vector.emplace_back(std::make_unique<MetadataMap>(metadata_map));
  // SETTINGS are required as part of the preface.
  ASSERT_EQ(nghttp2_submit_settings(client_->session(), NGHTTP2_FLAG_NONE, nullptr, 0), 0);
  // Validate both the ID = 0 special case and a non-zero ID not already bound to a stream (any ID >
  // 0 for this test).
  EXPECT_TRUE(client_->submitMetadata(metadata_vector, 0));
  EXPECT_TRUE(client_->submitMetadata(metadata_vector, 1000));
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
