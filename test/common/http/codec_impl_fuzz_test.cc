#include "envoy/stats/scope.h"

// Fuzzer for the H1/H2 codecs. This is similar in structure to
// //test/common/http/http2:codec_impl_test, where a client H2 codec is wired
// via shared memory to a server H2 codec and stream actions are applied. We
// fuzz the various client/server H1/H2 codec API operations and in addition
// apply fuzzing at the wire level by modeling explicit mutation, reordering and
// drain operations on the connection buffers between client and server.

#include <functional>

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"

#include "test/common/http/codec_impl_fuzz.pb.validate.h"
#include "test/common/http/http2/codec_impl_test_util.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;

namespace Envoy {
namespace Http {

namespace Http2Utility = ::Envoy::Http2::Utility;

// Force drain on each action, useful for figuring out what is going on when
// debugging.
constexpr bool DebugMode = false;

template <class T> T fromSanitizedHeaders(const test::fuzz::Headers& headers) {
  return Fuzz::fromHeaders<T>(headers, {"transfer-encoding"});
}

// Template specialization for TestRequestHeaderMapImpl to include a Host header. This guards
// against missing host headers in CONNECT requests that would have failed parsing on ingress.
// TODO(#10878): When proper error handling is introduced for non-dispatching codec calls, remove
// this and fail gracefully.
template <>
TestRequestHeaderMapImpl
fromSanitizedHeaders<TestRequestHeaderMapImpl>(const test::fuzz::Headers& headers) {
  return Fuzz::fromHeaders<TestRequestHeaderMapImpl>(headers, {"transfer-encoding"},
                                                     {":authority", ":method", ":path"});
}

// Convert from test proto Http1ServerSettings to Http1Settings.
Http1Settings fromHttp1Settings(const test::common::http::Http1ServerSettings& settings) {
  Http1Settings h1_settings;

  h1_settings.allow_absolute_url_ = settings.allow_absolute_url();
  h1_settings.accept_http_10_ = settings.accept_http_10();
  h1_settings.default_host_for_http_10_ = settings.default_host_for_http_10();

  return h1_settings;
}

envoy::config::core::v3::Http2ProtocolOptions
fromHttp2Settings(const test::common::http::Http2Settings& settings) {
  envoy::config::core::v3::Http2ProtocolOptions options(
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions()));
  // We apply an offset and modulo interpretation to settings to ensure that
  // they are valid. Rejecting invalid settings is orthogonal to the fuzzed
  // code.
  options.mutable_hpack_table_size()->set_value(settings.hpack_table_size());
  options.mutable_max_concurrent_streams()->set_value(
      Http2Utility::OptionsLimits::MIN_MAX_CONCURRENT_STREAMS +
      settings.max_concurrent_streams() %
          (1 + Http2Utility::OptionsLimits::MAX_MAX_CONCURRENT_STREAMS -
           Http2Utility::OptionsLimits::MIN_MAX_CONCURRENT_STREAMS));
  options.mutable_initial_stream_window_size()->set_value(
      Http2Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE +
      settings.initial_stream_window_size() %
          (1 + Http2Utility::OptionsLimits::MAX_INITIAL_STREAM_WINDOW_SIZE -
           Http2Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE));
  options.mutable_initial_connection_window_size()->set_value(
      Http2Utility::OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE +
      settings.initial_connection_window_size() %
          (1 + Http2Utility::OptionsLimits::MAX_INITIAL_CONNECTION_WINDOW_SIZE -
           Http2Utility::OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE));
  options.set_allow_metadata(true);
  return options;
}

using StreamResetCallbackFn = std::function<void()>;

// Internal representation of stream state. Encapsulates the stream state, mocks
// and encoders for both the request/response.
class HttpStream : public LinkedObject<HttpStream> {
public:
  // We track stream state here to prevent illegal operations, e.g. applying an
  // encodeData() to the codec after encodeTrailers(). This is necessary to
  // maintain the preconditions for operations on the codec at the API level. Of
  // course, it's the codecs must be robust to wire-level violations. We
  // explore these violations via MutateAction and SwapAction at the connection
  // buffer level.
  enum class StreamState : int { PendingHeaders, PendingDataOrTrailers, Closed };

  struct DirectionalState {
    // TODO(mattklein123): Split this more clearly into request and response directional state.
    RequestEncoder* request_encoder_;
    ResponseEncoder* response_encoder_;
    NiceMock<MockResponseDecoder> response_decoder_;
    NiceMock<MockRequestDecoder> request_decoder_;
    NiceMock<MockStreamCallbacks> stream_callbacks_;
    StreamState stream_state_;
    bool local_closed_{false};
    bool remote_closed_{false};
    uint32_t read_disable_count_{};

    bool isLocalOpen() const { return !local_closed_; }

    void closeLocal() {
      local_closed_ = true;
      if (local_closed_ && remote_closed_) {
        stream_state_ = StreamState::Closed;
      }
    }

    void closeRemote() {
      remote_closed_ = true;
      if (local_closed_ && remote_closed_) {
        stream_state_ = StreamState::Closed;
      }
    }
  } request_, response_;

  HttpStream(ClientConnection& client, const TestRequestHeaderMapImpl& request_headers,
             bool end_stream, StreamResetCallbackFn stream_reset_callback)
      : stream_reset_callback_(stream_reset_callback) {
    request_.request_encoder_ = &client.newStream(response_.response_decoder_);
    ON_CALL(request_.stream_callbacks_, onResetStream(_, _))
        .WillByDefault(InvokeWithoutArgs([this] {
          ENVOY_LOG_MISC(trace, "reset request for stream index {}", stream_index_);
          resetStream();
          stream_reset_callback_();
        }));
    ON_CALL(response_.stream_callbacks_, onResetStream(_, _))
        .WillByDefault(InvokeWithoutArgs([this] {
          ENVOY_LOG_MISC(trace, "reset response for stream index {}", stream_index_);
          // Reset the client stream when we know the server stream has been reset. This ensures
          // that the internal book keeping resetStream() below is consistent with the state of the
          // client codec state, which is necessary to prevent multiple simultaneous streams for the
          // HTTP/1 codec.
          request_.request_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
          resetStream();
          stream_reset_callback_();
        }));
    ON_CALL(request_.request_decoder_, decodeHeaders_(_, true))
        .WillByDefault(InvokeWithoutArgs([this] {
          // The HTTP/1 codec needs this to cleanup any latent stream resources.
          response_.response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
          request_.closeRemote();
        }));
    ON_CALL(request_.request_decoder_, decodeData(_, true)).WillByDefault(InvokeWithoutArgs([this] {
      // The HTTP/1 codec needs this to cleanup any latent stream resources.
      response_.response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
      request_.closeRemote();
    }));
    ON_CALL(request_.request_decoder_, decodeTrailers_(_)).WillByDefault(InvokeWithoutArgs([this] {
      // The HTTP/1 codec needs this to cleanup any latent stream resources.
      response_.response_encoder_->getStream().resetStream(StreamResetReason::LocalReset);
      request_.closeRemote();
    }));
    ON_CALL(response_.response_decoder_, decodeHeaders_(_, true))
        .WillByDefault(InvokeWithoutArgs([this] { response_.closeRemote(); }));
    ON_CALL(response_.response_decoder_, decodeData(_, true))
        .WillByDefault(InvokeWithoutArgs([this] { response_.closeRemote(); }));
    ON_CALL(response_.response_decoder_, decodeTrailers_(_))
        .WillByDefault(InvokeWithoutArgs([this] { response_.closeRemote(); }));
    if (!end_stream) {
      request_.request_encoder_->getStream().addCallbacks(request_.stream_callbacks_);
    }

    request_.request_encoder_->encodeHeaders(request_headers, end_stream);
    request_.stream_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
    response_.stream_state_ = StreamState::PendingHeaders;
  }

  void resetStream() {
    request_.closeLocal();
    request_.closeRemote();
    response_.closeLocal();
    response_.closeRemote();
  }

  // Some stream action applied in either the request or response direction.
  void directionalAction(DirectionalState& state,
                         const test::common::http::DirectionalAction& directional_action) {
    const bool end_stream = directional_action.end_stream();
    const bool response = &state == &response_;
    switch (directional_action.directional_action_selector_case()) {
    case test::common::http::DirectionalAction::kContinueHeaders: {
      if (state.isLocalOpen() && state.stream_state_ == StreamState::PendingHeaders) {
        auto headers =
            fromSanitizedHeaders<TestResponseHeaderMapImpl>(directional_action.continue_headers());
        headers.setReferenceKey(Headers::get().Status, "100");
        state.response_encoder_->encode100ContinueHeaders(headers);
      }
      break;
    }
    case test::common::http::DirectionalAction::kHeaders: {
      if (state.isLocalOpen() && state.stream_state_ == StreamState::PendingHeaders) {
        if (response) {
          auto headers =
              fromSanitizedHeaders<TestResponseHeaderMapImpl>(directional_action.headers());
          if (headers.Status() == nullptr) {
            headers.setReferenceKey(Headers::get().Status, "200");
          }
          state.response_encoder_->encodeHeaders(headers, end_stream);
        } else {
          state.request_encoder_->encodeHeaders(
              fromSanitizedHeaders<TestRequestHeaderMapImpl>(directional_action.headers()),
              end_stream);
        }
        if (end_stream) {
          state.closeLocal();
        } else {
          state.stream_state_ = StreamState::PendingDataOrTrailers;
        }
      }
      break;
    }
    case test::common::http::DirectionalAction::kData: {
      if (state.isLocalOpen() && state.stream_state_ == StreamState::PendingDataOrTrailers) {
        Buffer::OwnedImpl buf(std::string(directional_action.data() % (1024 * 1024), 'a'));
        if (response) {
          state.response_encoder_->encodeData(buf, end_stream);
        } else {
          state.request_encoder_->encodeData(buf, end_stream);
        }
        if (end_stream) {
          state.closeLocal();
        }
      }
      break;
    }
    case test::common::http::DirectionalAction::kDataValue: {
      if (state.isLocalOpen() && state.stream_state_ == StreamState::PendingDataOrTrailers) {
        Buffer::OwnedImpl buf(directional_action.data_value());
        if (response) {
          state.response_encoder_->encodeData(buf, end_stream);
        } else {
          state.request_encoder_->encodeData(buf, end_stream);
        }
        if (end_stream) {
          state.closeLocal();
        }
      }
      break;
    }
    case test::common::http::DirectionalAction::kTrailers: {
      if (state.isLocalOpen() && state.stream_state_ == StreamState::PendingDataOrTrailers) {
        if (response) {
          state.response_encoder_->encodeTrailers(
              fromSanitizedHeaders<TestResponseTrailerMapImpl>(directional_action.trailers()));
        } else {
          state.request_encoder_->encodeTrailers(
              fromSanitizedHeaders<TestRequestTrailerMapImpl>(directional_action.trailers()));
        }
        state.stream_state_ = StreamState::Closed;
        state.closeLocal();
      }
      break;
    }
    case test::common::http::DirectionalAction::kMetadata: {
      if (state.isLocalOpen() && state.stream_state_ != StreamState::Closed) {
        if (response) {
          state.response_encoder_->encodeMetadata(
              Fuzz::fromMetadata(directional_action.metadata()));
        } else {
          state.request_encoder_->encodeMetadata(Fuzz::fromMetadata(directional_action.metadata()));
        }
      }
      break;
    }
    case test::common::http::DirectionalAction::kResetStream: {
      if (state.stream_state_ != StreamState::Closed) {
        StreamEncoder* encoder;
        if (response) {
          encoder = state.response_encoder_;
        } else {
          encoder = state.request_encoder_;
        }
        encoder->getStream().resetStream(
            static_cast<Http::StreamResetReason>(directional_action.reset_stream()));
        request_.stream_state_ = response_.stream_state_ = StreamState::Closed;
      }
      break;
    }
    case test::common::http::DirectionalAction::kReadDisable: {
      if (state.stream_state_ != StreamState::Closed) {
        const bool disable = directional_action.read_disable();
        if (state.read_disable_count_ == 0 && !disable) {
          return;
        }
        if (disable) {
          ++state.read_disable_count_;
        } else {
          --state.read_disable_count_;
        }
        StreamEncoder* encoder;
        if (response) {
          encoder = state.response_encoder_;
        } else {
          encoder = state.request_encoder_;
        }
        encoder->getStream().readDisable(disable);
      }
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }

  void streamAction(const test::common::http::StreamAction& stream_action) {
    switch (stream_action.stream_action_selector_case()) {
    case test::common::http::StreamAction::kRequest: {
      ENVOY_LOG_MISC(debug, "Request stream action on {} in state {} {}", stream_index_,
                     static_cast<int>(request_.stream_state_),
                     static_cast<int>(response_.stream_state_));
      if (stream_action.has_dispatching_action()) {
        // Simulate some response action while dispatching request headers, data, or trailers. This
        // may happen as a result of a filter sending a direct response.
        ENVOY_LOG_MISC(debug, "Setting dispatching action  on {} in state {} {}", stream_index_,
                       static_cast<int>(request_.stream_state_),
                       static_cast<int>(response_.stream_state_));
        auto request_action = stream_action.dispatching_action().directional_action_selector_case();
        if (request_action == test::common::http::DirectionalAction::kHeaders) {
          EXPECT_CALL(request_.request_decoder_, decodeHeaders_(_, _))
              .WillOnce(InvokeWithoutArgs(
                  [&] { directionalAction(response_, stream_action.dispatching_action()); }));
        } else if (request_action == test::common::http::DirectionalAction::kData) {
          EXPECT_CALL(request_.request_decoder_, decodeData(_, _))
              .Times(testing::AtLeast(1))
              .WillRepeatedly(InvokeWithoutArgs(
                  [&] { directionalAction(response_, stream_action.dispatching_action()); }));
        } else if (request_action == test::common::http::DirectionalAction::kTrailers) {
          EXPECT_CALL(request_.request_decoder_, decodeTrailers_(_))
              .WillOnce(InvokeWithoutArgs(
                  [&] { directionalAction(response_, stream_action.dispatching_action()); }));
        }
      }
      // Perform the stream action.
      directionalAction(request_, stream_action.request());
      break;
    }
    case test::common::http::StreamAction::kResponse: {
      ENVOY_LOG_MISC(debug, "Response stream action on {} in state {} {}", stream_index_,
                     static_cast<int>(request_.stream_state_),
                     static_cast<int>(response_.stream_state_));
      directionalAction(response_, stream_action.response());
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
    ENVOY_LOG_MISC(debug, "Stream action complete");
  }

  bool active() const {
    return request_.stream_state_ != StreamState::Closed ||
           response_.stream_state_ != StreamState::Closed;
  }

  int32_t stream_index_{-1};
  StreamResetCallbackFn stream_reset_callback_;
};

// Buffer between client and server H1/H2 codecs. This models each write operation
// as adding a distinct fragment that might be reordered with other fragments in
// the buffer via swap() or modified with mutate().
class ReorderBuffer {
public:
  ReorderBuffer(Connection& connection, const bool& should_close_connection)
      : connection_(connection), should_close_connection_(should_close_connection) {}

  void add(Buffer::Instance& data) {
    bufs_.emplace_back();
    bufs_.back().move(data);
  }

  Http::Status drain() {
    Status status = Http::okStatus();
    while (!bufs_.empty()) {
      Buffer::OwnedImpl& buf = bufs_.front();
      while (buf.length() > 0) {
        if (should_close_connection_) {
          ENVOY_LOG_MISC(trace, "Buffer dispatch disabled, stopping drain");
          return codecClientError("preventing buffer drain due to connection closure");
        }
        status = connection_.dispatch(buf);
        if (!status.ok()) {
          ENVOY_LOG_MISC(trace, "Error status: {}", status.message());
          return status;
        }
      }
      bufs_.pop_front();
    }
    return status;
  }

  void mutate(uint32_t buffer, uint32_t offset, uint8_t value) {
    if (bufs_.empty()) {
      return;
    }
    Buffer::OwnedImpl& buf = bufs_[buffer % bufs_.size()];
    if (buf.length() == 0) {
      return;
    }
    uint8_t* p = reinterpret_cast<uint8_t*>(buf.linearize(buf.length())) + offset % buf.length();
    ENVOY_LOG_MISC(trace, "Mutating {} to {}", *p, value);
    *p = value;
  }

  void swap(uint32_t buffer) {
    if (bufs_.empty()) {
      return;
    }
    const uint32_t effective_index = buffer % bufs_.size();
    if (effective_index == 0) {
      return;
    }
    Buffer::OwnedImpl tmp;
    tmp.move(bufs_[0]);
    bufs_[0].move(bufs_[effective_index]);
    bufs_[effective_index].move(tmp);
  }

  bool empty() const { return bufs_.empty(); }

  Connection& connection_;
  std::deque<Buffer::OwnedImpl> bufs_;
  // A reference to a flag indicating whether the reorder buffer is allowed to dispatch data to
  // the connection (reference to should_close_connection).
  const bool& should_close_connection_;
};

using HttpStreamPtr = std::unique_ptr<HttpStream>;

namespace {

enum class HttpVersion { Http1, Http2 };

void codecFuzz(const test::common::http::CodecImplFuzzTestCase& input, HttpVersion http_version) {
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Network::MockConnection> client_connection;
  const envoy::config::core::v3::Http2ProtocolOptions client_http2_options{
      fromHttp2Settings(input.h2_settings().client())};
  const Http1Settings client_http1settings;
  NiceMock<MockConnectionCallbacks> client_callbacks;
  NiceMock<Network::MockConnection> server_connection;
  NiceMock<MockServerConnectionCallbacks> server_callbacks;
  NiceMock<Random::MockRandomGenerator> random;
  uint32_t max_request_headers_kb = Http::DEFAULT_MAX_REQUEST_HEADERS_KB;
  uint32_t max_request_headers_count = Http::DEFAULT_MAX_HEADERS_COUNT;
  uint32_t max_response_headers_count = Http::DEFAULT_MAX_HEADERS_COUNT;
  const envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action = envoy::config::core::v3::HttpProtocolOptions::ALLOW;

  Http1::CodecStats::AtomicPtr http1_stats;
  Http2::CodecStats::AtomicPtr http2_stats;
  ClientConnectionPtr client;
  ServerConnectionPtr server;
  const bool http2 = http_version == HttpVersion::Http2;

  if (http2) {
    client = std::make_unique<Http2::ClientConnectionImpl>(
        client_connection, client_callbacks, Http2::CodecStats::atomicGet(http2_stats, stats_store),
        random, client_http2_options, max_request_headers_kb, max_response_headers_count,
        Http2::ProdNghttp2SessionFactory::get());
  } else {
    client = std::make_unique<Http1::ClientConnectionImpl>(
        client_connection, Http1::CodecStats::atomicGet(http1_stats, stats_store), client_callbacks,
        client_http1settings, max_response_headers_count);
  }

  if (http2) {
    const envoy::config::core::v3::Http2ProtocolOptions server_http2_options{
        fromHttp2Settings(input.h2_settings().server())};
    server = std::make_unique<Http2::ServerConnectionImpl>(
        server_connection, server_callbacks, Http2::CodecStats::atomicGet(http2_stats, stats_store),
        random, server_http2_options, max_request_headers_kb, max_request_headers_count,
        headers_with_underscores_action);
  } else {
    const Http1Settings server_http1settings{fromHttp1Settings(input.h1_settings().server())};
    server = std::make_unique<Http1::ServerConnectionImpl>(
        server_connection, Http1::CodecStats::atomicGet(http1_stats, stats_store), server_callbacks,
        server_http1settings, max_request_headers_kb, max_request_headers_count,
        headers_with_underscores_action);
  }

  // We track whether the connection should be closed for HTTP/1, since stream resets imply
  // connection closes.
  bool should_close_connection = false;

  // The buffers will be blocked from dispatching data if should_close_connection is set to true.
  // This prevents sending data if a stream reset occurs during the test cleanup when using HTTP/1.
  ReorderBuffer client_write_buf{*server, should_close_connection};
  ReorderBuffer server_write_buf{*client, should_close_connection};

  ON_CALL(client_connection, write(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
        ENVOY_LOG_MISC(trace, "client -> server {} bytes", data.length());
        client_write_buf.add(data);
      }));
  ON_CALL(server_connection, write(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
        ENVOY_LOG_MISC(trace, "server -> client {} bytes: {}", data.length(), data.toString());
        server_write_buf.add(data);
      }));

  // We hold Streams in pending_streams between the request encodeHeaders in the
  // Stream constructor and server newStream() callback, where we learn about
  // the response encoder and can complete Stream initialization.
  std::list<HttpStreamPtr> pending_streams;
  std::list<HttpStreamPtr> streams;
  // For new streams when we aren't expecting one (e.g. as a result of a mutation).
  NiceMock<MockRequestDecoder> orphan_request_decoder;

  ON_CALL(server_callbacks, newStream(_, _))
      .WillByDefault(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        if (pending_streams.empty()) {
          return orphan_request_decoder;
        }
        auto stream_ptr = pending_streams.front()->removeFromList(pending_streams);
        HttpStream* const stream = stream_ptr.get();
        LinkedList::moveIntoListBack(std::move(stream_ptr), streams);
        stream->response_.response_encoder_ = &encoder;
        encoder.getStream().addCallbacks(stream->response_.stream_callbacks_);
        stream->stream_index_ = streams.size() - 1;
        return stream->request_.request_decoder_;
      }));

  auto client_server_buf_drain = [&client_write_buf, &server_write_buf] {
    Http::Status status = Http::okStatus();
    while (!client_write_buf.empty() || !server_write_buf.empty()) {
      status = client_write_buf.drain();
      if (!status.ok()) {
        return status;
      }
      status = server_write_buf.drain();
      if (!status.ok()) {
        return status;
      }
    }
    return status;
  };

  constexpr auto max_actions = 1024;
  bool codec_error = false;
  for (int i = 0; i < std::min(max_actions, input.actions().size()) && !should_close_connection &&
                  !codec_error;
       ++i) {
    const auto& action = input.actions(i);
    ENVOY_LOG_MISC(trace, "action {} with {} streams", action.DebugString(), streams.size());
    switch (action.action_selector_case()) {
    case test::common::http::Action::kNewStream: {
      if (!http2) {
        // HTTP/1 codec needs to have existing streams complete, so make it
        // easier to achieve a successful multi-stream example by flushing.
        if (!client_server_buf_drain().ok()) {
          codec_error = true;
          break;
        }
        // HTTP/1 client codec can only have a single active stream.
        if (!pending_streams.empty() || (!streams.empty() && streams.back()->active())) {
          ENVOY_LOG_MISC(trace, "Skipping new stream as HTTP/1 and already have existing stream");
          continue;
        }
      }
      HttpStreamPtr stream = std::make_unique<HttpStream>(
          *client,
          fromSanitizedHeaders<TestRequestHeaderMapImpl>(action.new_stream().request_headers()),
          action.new_stream().end_stream(), [&should_close_connection, http2]() {
            // HTTP/1 codec has stream reset implying connection close.
            if (!http2) {
              should_close_connection = true;
            }
          });
      LinkedList::moveIntoListBack(std::move(stream), pending_streams);
      break;
    }
    case test::common::http::Action::kStreamAction: {
      const auto& stream_action = action.stream_action();
      if (streams.empty()) {
        break;
      }
      // Index into list of created streams (not HTTP/2 level stream ID).
      const uint32_t stream_id = stream_action.stream_id() % streams.size();
      ENVOY_LOG_MISC(trace, "action for stream index {}", stream_id);
      (*std::next(streams.begin(), stream_id))->streamAction(stream_action);
      break;
    }
    case test::common::http::Action::kMutate: {
      const auto& mutate = action.mutate();
      ReorderBuffer& write_buf = mutate.server() ? server_write_buf : client_write_buf;
      write_buf.mutate(mutate.buffer(), mutate.offset(), mutate.value());
      break;
    }
    case test::common::http::Action::kSwapBuffer: {
      const auto& swap_buffer = action.swap_buffer();
      ReorderBuffer& write_buf = swap_buffer.server() ? server_write_buf : client_write_buf;
      write_buf.swap(swap_buffer.buffer());
      break;
    }
    case test::common::http::Action::kClientDrain: {
      if (!client_write_buf.drain().ok()) {
        codec_error = true;
        break;
      }
      break;
    }
    case test::common::http::Action::kServerDrain: {
      if (!server_write_buf.drain().ok()) {
        codec_error = true;
        break;
      }
      break;
    }
    case test::common::http::Action::kQuiesceDrain: {
      if (!client_server_buf_drain().ok()) {
        codec_error = true;
        break;
      }
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
    if (DebugMode && !should_close_connection && !codec_error) {
      if (!client_server_buf_drain().ok()) {
        codec_error = true;
        break;
      }
    }
  }
  // Drain all remaining buffers, unless the connection is effectively closed.
  if (!should_close_connection && !codec_error) {
    if (!client_server_buf_drain().ok()) {
      codec_error = true;
    }
  }
  if (!codec_error && http2) {
    dynamic_cast<Http2::ClientConnectionImpl&>(*client).goAway();
    dynamic_cast<Http2::ServerConnectionImpl&>(*server).goAway();
  }
}

} // namespace

// Fuzz the H1/H2 codec implementations.
DEFINE_PROTO_FUZZER(const test::common::http::CodecImplFuzzTestCase& input) {
  try {
    // Validate input early.
    TestUtility::validate(input);
    codecFuzz(input, HttpVersion::Http1);
    codecFuzz(input, HttpVersion::Http2);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace Http
} // namespace Envoy
