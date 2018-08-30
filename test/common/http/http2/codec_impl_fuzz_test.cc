#include "envoy/stats/scope.h"

// Fuzzer for the H2 codec. This is similar in structure to
// //test/common/http/http2:codec_impl_test, where a client H2 codec is wired
// via shared memory to a server H2 codec and stream actions are applied. We
// fuzz the various client/server H2 codec API operations and in addition apply
// fuzzing at the wire level by modeling explicit mutation, reordering and drain
// operations on the connection buffers between client and server.

#include <functional>

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_impl.h"

#include "test/common/http/http2/codec_impl_fuzz.pb.h"
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
namespace Http2 {

class TestServerConnectionImpl : public ServerConnectionImpl {
public:
  TestServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks,
                           Stats::Scope& scope, const Http2Settings& http2_settings)
      : ServerConnectionImpl(connection, callbacks, scope, http2_settings) {}
  nghttp2_session* session() { return session_; }
  using ServerConnectionImpl::getStream;
};

class TestClientConnectionImpl : public ClientConnectionImpl {
public:
  TestClientConnectionImpl(Network::Connection& connection, Http::ConnectionCallbacks& callbacks,
                           Stats::Scope& scope, const Http2Settings& http2_settings)
      : ClientConnectionImpl(connection, callbacks, scope, http2_settings) {}
  nghttp2_session* session() { return session_; }
  using ClientConnectionImpl::getStream;
};

// Convert from test proto Http2Settings to Http2Settings.
Http2Settings fromHttp2Settings(const test::common::http::http2::Http2Settings& settings) {
  Http2Settings h2_settings;
  // We apply an offset and modulo interpretation to settings to ensure that
  // they are valid. Rejecting invalid settings is orthogonal to the fuzzed
  // code.
  h2_settings.hpack_table_size_ = settings.hpack_table_size();
  h2_settings.max_concurrent_streams_ =
      Http2Settings::MIN_MAX_CONCURRENT_STREAMS +
      settings.max_concurrent_streams() % (1 + Http2Settings::MAX_MAX_CONCURRENT_STREAMS -
                                           Http2Settings::MIN_MAX_CONCURRENT_STREAMS);
  h2_settings.initial_stream_window_size_ =
      Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE +
      settings.initial_stream_window_size() % (1 + Http2Settings::MAX_INITIAL_STREAM_WINDOW_SIZE -
                                               Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE);
  h2_settings.initial_connection_window_size_ =
      Http2Settings::MIN_INITIAL_CONNECTION_WINDOW_SIZE +
      settings.initial_connection_window_size() %
          (1 + Http2Settings::MAX_INITIAL_CONNECTION_WINDOW_SIZE -
           Http2Settings::MIN_INITIAL_CONNECTION_WINDOW_SIZE);
  return h2_settings;
}

// Internal representation of stream state. Encapsulates the stream state, mocks
// and encoders for both the request/response.
class Stream : public LinkedObject<Stream> {
public:
  // We track stream state here to prevent illegal operations, e.g. applying an
  // encodeData() to the codec after encodeTrailers(). This is necessary to
  // maintain the preconditions for operations on the codec at the API level. Of
  // course, it's the codecs must be robust to wire-level violations. We
  // explore these violations via MutateAction and SwapAction at the connection
  // buffer level.
  enum class StreamState { PendingHeaders, PendingDataOrTrailers, Closed };

  struct DirectionalState {
    StreamEncoder* encoder_;
    NiceMock<MockStreamDecoder> decoder_;
    NiceMock<MockStreamCallbacks> stream_callbacks_;
    StreamState stream_state_;
    uint32_t read_disable_count_{};
  } request_, response_;

  Stream(TestClientConnectionImpl& client, const TestHeaderMapImpl& request_headers,
         bool end_stream) {
    request_.encoder_ = &client.newStream(response_.decoder_);
    ON_CALL(request_.stream_callbacks_, onResetStream(_)).WillByDefault(InvokeWithoutArgs([this] {
      ENVOY_LOG_MISC(trace, "reset request for stream index {}", stream_index_);
      resetStream();
    }));
    ON_CALL(response_.stream_callbacks_, onResetStream(_)).WillByDefault(InvokeWithoutArgs([this] {
      ENVOY_LOG_MISC(trace, "reset response for stream index {}", stream_index_);
      resetStream();
    }));
    request_.encoder_->encodeHeaders(request_headers, end_stream);
    if (!end_stream) {
      request_.encoder_->getStream().addCallbacks(request_.stream_callbacks_);
    }
    request_.stream_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
    response_.stream_state_ = StreamState::PendingHeaders;
  }

  void resetStream() { request_.stream_state_ = response_.stream_state_ = StreamState::Closed; }

  // Some stream action applied in either the request or resposne direction.
  void directionalAction(DirectionalState& state,
                         const test::common::http::http2::DirectionalAction& directional_action) {
    const bool end_stream = directional_action.end_stream();
    switch (directional_action.directional_action_selector_case()) {
    case test::common::http::http2::DirectionalAction::kContinueHeaders: {
      if (state.stream_state_ == StreamState::PendingHeaders) {
        Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(directional_action.continue_headers());
        headers.setReferenceKey(Headers::get().Status, "100");
        state.encoder_->encode100ContinueHeaders(headers);
      }
      break;
    }
    case test::common::http::http2::DirectionalAction::kHeaders: {
      if (state.stream_state_ == StreamState::PendingHeaders) {
        state.encoder_->encodeHeaders(Fuzz::fromHeaders(directional_action.headers()), end_stream);
        state.stream_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      }
      break;
    }
    case test::common::http::http2::DirectionalAction::kData: {
      if (state.stream_state_ == StreamState::PendingDataOrTrailers) {
        Buffer::OwnedImpl buf(std::string(directional_action.data() % (1024 * 1024), 'a'));
        state.encoder_->encodeData(buf, end_stream);
        state.stream_state_ = end_stream ? StreamState::Closed : StreamState::PendingDataOrTrailers;
      }
      break;
    }
    case test::common::http::http2::DirectionalAction::kTrailers: {
      if (state.stream_state_ == StreamState::PendingDataOrTrailers) {
        state.encoder_->encodeTrailers(Fuzz::fromHeaders(directional_action.trailers()));
        state.stream_state_ = StreamState::Closed;
      }
      break;
    }
    case test::common::http::http2::DirectionalAction::kResetStream: {
      if (state.stream_state_ != StreamState::Closed) {
        state.encoder_->getStream().resetStream(
            static_cast<Http::StreamResetReason>(directional_action.reset_stream()));
        request_.stream_state_ = response_.stream_state_ = StreamState::Closed;
      }
      break;
    }
    case test::common::http::http2::DirectionalAction::kReadDisable: {
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
        state.encoder_->getStream().readDisable(disable);
      }
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }

  void streamAction(const test::common::http::http2::StreamAction& stream_action) {
    switch (stream_action.stream_action_selector_case()) {
    case test::common::http::http2::StreamAction::kRequest: {
      directionalAction(request_, stream_action.request());
      break;
    }
    case test::common::http::http2::StreamAction::kResponse: {
      directionalAction(response_, stream_action.response());
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }

  int32_t stream_index_{-1};
};

// Buffer between client and server H2 codecs. This models each write operation
// as adding a distinct fragment that might be reordered with other fragments in
// the buffer via swap() or modified with mutate().
class ReorderBuffer {
public:
  ReorderBuffer(ConnectionImpl& connection) : connection_(connection) {}

  void add(const Buffer::Instance& data) { bufs_.emplace_back(data); }

  void drain() {
    while (!bufs_.empty()) {
      Buffer::OwnedImpl& buf = bufs_.front();
      while (buf.length() > 0) {
        connection_.dispatch(buf);
      }
      bufs_.pop_front();
    }
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

  ConnectionImpl& connection_;
  std::deque<Buffer::OwnedImpl> bufs_;
};

typedef std::unique_ptr<Stream> StreamPtr;

// Fuzz the H2 codec implementation.
DEFINE_PROTO_FUZZER(const test::common::http::http2::CodecImplFuzzTestCase& input) {
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Network::MockConnection> client_connection;
  const Http2Settings client_http2settings{fromHttp2Settings(input.client_settings())};
  NiceMock<MockConnectionCallbacks> client_callbacks;
  TestClientConnectionImpl client(client_connection, client_callbacks, stats_store,
                                  client_http2settings);

  NiceMock<Network::MockConnection> server_connection;
  const Http2Settings server_http2settings{fromHttp2Settings(input.server_settings())};
  NiceMock<MockServerConnectionCallbacks> server_callbacks;
  TestServerConnectionImpl server(server_connection, server_callbacks, stats_store,
                                  server_http2settings);

  ReorderBuffer client_write_buf{server};
  ReorderBuffer server_write_buf{client};

  ON_CALL(client_connection, write(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
        ENVOY_LOG_MISC(trace, "client -> server {} bytes", data.length());
        client_write_buf.add(data);
      }));
  ON_CALL(server_connection, write(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) -> void {
        ENVOY_LOG_MISC(trace, "server -> client {} bytes", data.length());
        server_write_buf.add(data);
      }));

  // We hold Streams in pending_streams between the request encodeHeaders in the
  // Stream constructor and server newStream() callback, where we learn about
  // the response encoder and can complete Stream initialization.
  std::list<StreamPtr> pending_streams;
  std::list<StreamPtr> streams;
  // For new streams when we aren't expecting one (e.g. as a result of a mutation).
  NiceMock<MockStreamDecoder> orphan_request_decoder;

  ON_CALL(server_callbacks, newStream(_))
      .WillByDefault(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        if (pending_streams.empty()) {
          return orphan_request_decoder;
        }
        auto stream_ptr = pending_streams.front()->removeFromList(pending_streams);
        Stream* const stream = stream_ptr.get();
        stream_ptr->moveIntoListBack(std::move(stream_ptr), streams);
        stream->response_.encoder_ = &encoder;
        encoder.getStream().addCallbacks(stream->response_.stream_callbacks_);
        stream->stream_index_ = streams.size() - 1;
        return stream->request_.decoder_;
      }));

  try {
    for (const auto& action : input.actions()) {
      ENVOY_LOG_MISC(trace, "action {} with {} streams", action.DebugString(), streams.size());
      switch (action.action_selector_case()) {
      case test::common::http::http2::Action::kNewStream: {
        StreamPtr stream = std::make_unique<Stream>(
            client, Fuzz::fromHeaders(action.new_stream().request_headers()),
            action.new_stream().end_stream());
        stream->moveIntoListBack(std::move(stream), pending_streams);
        break;
      }
      case test::common::http::http2::Action::kStreamAction: {
        const auto& stream_action = action.stream_action();
        if (streams.empty()) {
          break;
        }
        const uint32_t stream_id = stream_action.stream_id() % streams.size();
        ENVOY_LOG_MISC(trace, "action for stream index {}", stream_id);
        (*std::next(streams.begin(), stream_id))->streamAction(stream_action);
        break;
      }
      case test::common::http::http2::Action::kMutate: {
        const auto& mutate = action.mutate();
        ReorderBuffer& write_buf = mutate.server() ? server_write_buf : client_write_buf;
        write_buf.mutate(mutate.buffer(), mutate.offset(), mutate.value());
        break;
      }
      case test::common::http::http2::Action::kSwapBuffer: {
        const auto& swap_buffer = action.swap_buffer();
        ReorderBuffer& write_buf = swap_buffer.server() ? server_write_buf : client_write_buf;
        write_buf.swap(swap_buffer.buffer());
        break;
      }
      case test::common::http::http2::Action::kClientDrain: {
        client_write_buf.drain();
        break;
      }
      case test::common::http::http2::Action::kServerDrain: {
        server_write_buf.drain();
        break;
      }
      case test::common::http::http2::Action::kQuiesceDrain: {
        while (!client_write_buf.empty() || !server_write_buf.empty()) {
          client_write_buf.drain();
          server_write_buf.drain();
        }
        break;
      }
      default:
        // Maybe nothing is set?
        break;
      }
    }
    client.goAway();
    server.goAway();
  } catch (EnvoyException&) {
  }
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
