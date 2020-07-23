#include <cstdint>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/stats/scope.h"

#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/runtime/runtime_features.h"

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
    Http::Status dispatch(const Buffer::Instance& data, Connection& connection) {
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
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.new_codec_behavior")) {
      client_ = std::make_unique<TestClientConnectionImplNew>(
          client_connection_, client_callbacks_, client_stats_store_, client_http2_options_,
          max_request_headers_kb_, max_response_headers_count_,
          ProdNghttp2SessionFactoryNew::get());
      server_ = std::make_unique<TestServerConnectionImplNew>(
          server_connection_, server_callbacks_, server_stats_store_, server_http2_options_,
          max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
    } else {
      client_ = std::make_unique<TestClientConnectionImplLegacy>(
          client_connection_, client_callbacks_, client_stats_store_, client_http2_options_,
          max_request_headers_kb_, max_response_headers_count_,
          ProdNghttp2SessionFactoryLegacy::get());
      server_ = std::make_unique<TestServerConnectionImplLegacy>(
          server_connection_, server_callbacks_, server_stats_store_, server_http2_options_,
          max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
    }
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
  std::unique_ptr<TestClientConnection> client_;
  ConnectionWrapper client_wrapper_;
  Stats::TestUtil::TestStore server_stats_store_;
  envoy::config::core::v3::Http2ProtocolOptions server_http2_options_;
  NiceMock<Network::MockConnection> server_connection_;
  MockServerConnectionCallbacks server_callbacks_;
  std::unique_ptr<TestServerConnection> server_;
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
    request_encoder_->encodeHeaders(request_headers, false);

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
    request_encoder_->encodeHeaders(request_headers, false);

    // HTTP/2 codec does not send empty DATA frames with no END_STREAM flag.
    // To make this work, send raw bytes representing empty DATA frames bypassing client codec.
    Http2Frame emptyDataFrame = Http2Frame::makeEmptyDataFrame(0);
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
  request_encoder_->encodeHeaders(request_headers, true);

  EXPECT_CALL(client_callbacks_, onGoAway(_));
  server_->shutdownNotice();
  server_->goAway();

  TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, true));
  response_encoder_->encodeHeaders(response_headers, true);
}

// 100 response followed by 200 results in a [decode100ContinueHeaders, decodeHeaders] sequence.
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

// nghttp2 rejects trailers with :status.
TEST_P(Http2CodecImplTest, TrailerStatus) {
  initialize();

  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder_, decodeHeaders_(_, true));
  request_encoder_->encodeHeaders(request_headers, true);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  auto status = client_wrapper_.dispatch(Buffer::OwnedImpl(), *client_);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(1, client_stats_store_.counter("http2.rx_messaging_error").value());
  expectDetailsRequest("http2.violation.of.messaging.rule");
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
  request_encoder_->encodeHeaders(request_headers, true);

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

  EXPECT_THROW(request_encoder_->encodeHeaders(TestRequestHeaderMapImpl{}, true), ServerCodecError);
  EXPECT_EQ(1, server_stats_store_.counter("http2.rx_messaging_error").value());
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
  auto status = server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);
  EXPECT_TRUE(status.ok());
  expectDetailsResponse("http2.violation.of.messaging.rule");
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

TEST_P(Http2CodecImplTest, TrailingHeadersLargeClientBody) {
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
  request_encoder_->encodeHeaders(request_headers, true);
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
  auto status = server_wrapper_.dispatch(Buffer::OwnedImpl(), *server_);
  EXPECT_TRUE(status.ok());
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
  request_encoder_->encodeHeaders(request_headers, false);

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
  EXPECT_EQ(initial_stream_window, server_->getStreamUnconsumedBytes(1));

  // Now that the flow control window is full, further data causes the send buffer to back up.
  Buffer::OwnedImpl more_long_data(std::string(initial_stream_window, 'a'));
  request_encoder_->encodeData(more_long_data, false);
  EXPECT_EQ(initial_stream_window, client_->getStreamPendingSendDataLength(1));
  EXPECT_EQ(initial_stream_window,
            TestUtility::findGauge(client_stats_store_, "http2.pending_send_bytes")->value());
  EXPECT_EQ(initial_stream_window, server_->getStreamUnconsumedBytes(1));

  // If we go over the limit, the stream callbacks should fire.
  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  Buffer::OwnedImpl last_byte("!");
  request_encoder_->encodeData(last_byte, false);
  EXPECT_EQ(initial_stream_window + 1, client_->getStreamPendingSendDataLength(1));
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
  EXPECT_EQ(0, client_->getStreamPendingSendDataLength(1));
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
  EXPECT_EQ(initial_stream_window, server_->getStreamUnconsumedBytes(1));
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
  request_encoder_->encodeHeaders(request_headers, false);

  // Set artificially small watermarks to make the recv buffer easy to overrun. In production,
  // the recv buffer can be overrun by a client which negotiates a larger
  // SETTINGS_MAX_FRAME_SIZE but there's no current easy way to tweak that in
  // envoy (without sending raw HTTP/2 frames) so we lower the buffer limit instead.
  server_->setStreamWriteBufferWatermarks(1, 10, 20);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  request_encoder_->encodeHeaders(request_headers, true);

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
  http2OptionsFromTuple(client_http2_options_, ::testing::get<0>(GetParam()));
  http2OptionsFromTuple(server_http2_options_, ::testing::get<1>(GetParam()));
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.new_codec_behavior")) {
    client_ = std::make_unique<TestClientConnectionImplNew>(
        client_connection_, client_callbacks_, client_stats_store_, client_http2_options_,
        max_request_headers_kb_, max_response_headers_count_, ProdNghttp2SessionFactoryNew::get());
    server_ = std::make_unique<TestServerConnectionImplNew>(
        server_connection_, server_callbacks_, server_stats_store_, server_http2_options_,
        max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);

  } else {
    client_ = std::make_unique<TestClientConnectionImplLegacy>(
        client_connection_, client_callbacks_, client_stats_store_, client_http2_options_,
        max_request_headers_kb_, max_response_headers_count_,
        ProdNghttp2SessionFactoryLegacy::get());
    server_ = std::make_unique<TestServerConnectionImplLegacy>(
        server_connection_, server_callbacks_, server_stats_store_, server_http2_options_,
        max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
  }
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
  request_encoder_->encodeHeaders(request_headers, false);
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
  request_encoder_->encodeHeaders(request_headers, false);
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
  EXPECT_CALL(server_stream_callbacks_, onResetStream(_, _)).Times(1);
  request_encoder_->encodeHeaders(request_headers, false);
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
  request_encoder_->encodeHeaders(request_headers, false);
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
  EXPECT_CALL(client_callbacks_, onGoAway(_));
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
  request_encoder_->encodeHeaders(request_headers, false);

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

  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);
  EXPECT_EQ(ack_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_control_flood").value());
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
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1;
       ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }

  EXPECT_CALL(server_connection_, write(_, _))
      .Times(CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES + 1);
  EXPECT_NO_THROW(client_->sendPendingFrames());
}

// Verify that outbound control frame counter decreases when send buffer is drained
TEST_P(Http2CodecImplTest, PingFloodCounterReset) {
  // Ping frames are 17 bytes each so 237 full frames and a partial frame fit in the current min
  // size for buffer slices. Setting the limit to 2x+1 the number that fits in a single slice allows
  // the logic below that verifies drain and overflow thresholds.
  static const int kMaxOutboundControlFrames = 475;
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

  // Drain floor(kMaxOutboundFrames / 2) slices from the send buffer
  buffer.drain(buffer.length() / 2);

  // Send floor(kMaxOutboundFrames / 2) more pings.
  for (int i = 0; i < kMaxOutboundControlFrames / 2; ++i) {
    EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  }
  // The number of outbound frames should be half of max so the connection should not be
  // terminated.
  EXPECT_NO_THROW(client_->sendPendingFrames());
  EXPECT_EQ(ack_count, kMaxOutboundControlFrames + kMaxOutboundControlFrames / 2);

  // 1 more ping frame should overflow the outbound frame limit.
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);
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
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1; ++i) {
    EXPECT_NO_THROW(response_encoder_->encodeHeaders(response_headers, false));
  }
  // Presently flood mitigation is done only when processing downstream data
  // So we need to send stream from downstream client to trigger mitigation
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 1);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
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
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }
  // Presently flood mitigation is done only when processing downstream data
  // So we need to send stream from downstream client to trigger mitigation
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);

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
  request_encoder_->encodeHeaders(request_headers, false);

  // +2 is to account for HEADERS and PING ACK, that is used to trigger mitigation
  EXPECT_CALL(server_connection_, write(_, _))
      .Times(CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES + 2);
  EXPECT_CALL(response_decoder_, decodeHeaders_(_, false)).Times(1);
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
  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);
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
  for (uint32_t i = 0; i < CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES - 1; ++i) {
    Buffer::OwnedImpl data("0");
    EXPECT_NO_THROW(response_encoder_->encodeData(data, false));
  }
  // Send one PING frame above the outbound queue size limit
  EXPECT_EQ(0, nghttp2_submit_ping(client_->session(), NGHTTP2_FLAG_NONE, nullptr));
  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);

  EXPECT_EQ(frame_count, CommonUtility::OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES);
  EXPECT_EQ(1, server_stats_store_.counter("http2.outbound_flood").value());
}

TEST_P(Http2CodecImplTest, PriorityFlood) {
  priorityFlood();
  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);
}

TEST_P(Http2CodecImplTest, PriorityFloodOverride) {
  max_inbound_priority_frames_per_stream_ = 2147483647;

  priorityFlood();
  EXPECT_NO_THROW(client_->sendPendingFrames());
}

TEST_P(Http2CodecImplTest, WindowUpdateFlood) {
  windowUpdateFlood();
  EXPECT_THROW(client_->sendPendingFrames(), ServerCodecError);
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
  auto status = server_wrapper_.dispatch(data, *server_);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(isBufferFloodError(status));
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
  request_encoder_->encodeHeaders(request_headers, false);
}

template <typename, typename> class TestNghttp2SessionFactory;

// Test client for H/2 METADATA frame edge cases.
template <typename TestClientConnectionImplType>
class MetadataTestClientConnectionImpl : public TestClientConnectionImplType {
public:
  MetadataTestClientConnectionImpl(
      Network::Connection& connection, Http::ConnectionCallbacks& callbacks, Stats::Scope& scope,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
      typename TestClientConnectionImplType::SessionFactory& http2_session_factory)
      : TestClientConnectionImplType(connection, callbacks, scope, http2_options,
                                     max_request_headers_kb, max_request_headers_count,
                                     http2_session_factory) {}

  // Overrides TestClientConnectionImpl::submitMetadata().
  bool submitMetadata(const MetadataMapVector& metadata_map_vector, int32_t stream_id) override {
    // Creates metadata payload.
    encoder_.createPayload(metadata_map_vector);
    for (uint8_t flags : encoder_.payloadFrameFlagBytes()) {
      int result =
          nghttp2_submit_extension(TestClientConnectionImplType::session(),
                                   ::Envoy::Http::METADATA_FRAME_TYPE, flags, stream_id, nullptr);
      if (result != 0) {
        return false;
      }
    }
    // Triggers nghttp2 to populate the payloads of the METADATA frames.
    int result = nghttp2_session_send(TestClientConnectionImplType::session());
    return result == 0;
  }

protected:
  template <typename, typename> friend class TestNghttp2SessionFactory;

  MetadataEncoder encoder_;
};

using MetadataTestClientConnectionImplNew =
    MetadataTestClientConnectionImpl<TestClientConnectionImplNew>;
using MetadataTestClientConnectionImplLegacy =
    MetadataTestClientConnectionImpl<TestClientConnectionImplLegacy>;

struct Nghttp2SessionFactoryDeleter {
  virtual ~Nghttp2SessionFactoryDeleter() = default;
};

template <typename Nghttp2SessionFactoryType, typename TestClientConnectionImplType>
class TestNghttp2SessionFactory : public Nghttp2SessionFactoryType,
                                  public Nghttp2SessionFactoryDeleter {
public:
  ~TestNghttp2SessionFactory() override {
    nghttp2_session_callbacks_del(callbacks_);
    nghttp2_option_del(options_);
  }

  nghttp2_session* create(const nghttp2_session_callbacks*,
                          typename Nghttp2SessionFactoryType::ConnectionImplType* connection,
                          const nghttp2_option*) override {
    // Only need to provide callbacks required to send METADATA frames.
    nghttp2_session_callbacks_new(&callbacks_);
    nghttp2_session_callbacks_set_pack_extension_callback(
        callbacks_,
        [](nghttp2_session*, uint8_t* data, size_t length, const nghttp2_frame*,
           void* user_data) -> ssize_t {
          // Double cast required due to multiple inheritance.
          return static_cast<MetadataTestClientConnectionImpl<TestClientConnectionImplType>*>(
                     static_cast<typename Nghttp2SessionFactoryType::ConnectionImplType*>(
                         user_data))
              ->encoder_.packNextFramePayload(data, length);
        });
    nghttp2_session_callbacks_set_send_callback(
        callbacks_,
        [](nghttp2_session*, const uint8_t* data, size_t length, int, void* user_data) -> ssize_t {
          // Cast down to MetadataTestClientConnectionImpl to leverage friendship.
          return static_cast<MetadataTestClientConnectionImpl<TestClientConnectionImplType>*>(
                     static_cast<typename Nghttp2SessionFactoryType::ConnectionImplType*>(
                         user_data))
              ->onSend(data, length);
        });
    nghttp2_option_new(&options_);
    nghttp2_option_set_user_recv_extension_type(options_, METADATA_FRAME_TYPE);
    nghttp2_session* session;
    nghttp2_session_client_new2(&session, callbacks_, connection, options_);
    return session;
  }

  void init(nghttp2_session*, typename Nghttp2SessionFactoryType::ConnectionImplType*,
            const envoy::config::core::v3::Http2ProtocolOptions&) override {}

private:
  nghttp2_session_callbacks* callbacks_;
  nghttp2_option* options_;
};

using TestNghttp2SessionFactoryNew =
    TestNghttp2SessionFactory<ProdNghttp2SessionFactory, TestClientConnectionImplNew>;
using TestNghttp2SessionFactoryLegacy =
    TestNghttp2SessionFactory<Envoy::Http::Legacy::Http2::ProdNghttp2SessionFactory,
                              TestClientConnectionImplLegacy>;

class Http2CodecMetadataTest : public Http2CodecImplTestFixture, public ::testing::Test {
public:
  Http2CodecMetadataTest() = default;

protected:
  void initialize() override {
    allow_metadata_ = true;
    http2OptionsFromTuple(client_http2_options_, client_settings_);
    http2OptionsFromTuple(server_http2_options_, server_settings_);
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.new_codec_behavior")) {
      std::unique_ptr<TestNghttp2SessionFactoryNew> session_factory =
          std::make_unique<TestNghttp2SessionFactoryNew>();
      client_ = std::make_unique<MetadataTestClientConnectionImplNew>(
          client_connection_, client_callbacks_, client_stats_store_, client_http2_options_,
          max_request_headers_kb_, max_response_headers_count_, *session_factory);
      server_ = std::make_unique<TestServerConnectionImplNew>(
          server_connection_, server_callbacks_, server_stats_store_, server_http2_options_,
          max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
      http2_session_factory_ = std::move(session_factory);
    } else {
      std::unique_ptr<TestNghttp2SessionFactoryLegacy> session_factory =
          std::make_unique<TestNghttp2SessionFactoryLegacy>();
      client_ = std::make_unique<MetadataTestClientConnectionImplLegacy>(
          client_connection_, client_callbacks_, client_stats_store_, client_http2_options_,
          max_request_headers_kb_, max_response_headers_count_, *session_factory);
      server_ = std::make_unique<TestServerConnectionImplLegacy>(
          server_connection_, server_callbacks_, server_stats_store_, server_http2_options_,
          max_request_headers_kb_, max_request_headers_count_, headers_with_underscores_action_);
      http2_session_factory_ = std::move(session_factory);
    }
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
  std::unique_ptr<Nghttp2SessionFactoryDeleter> http2_session_factory_;
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
