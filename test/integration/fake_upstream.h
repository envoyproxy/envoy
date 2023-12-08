#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/stats/scope.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/basic_resource_impl.h"
#include "source/common/common/callback_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/http3/codec_stats.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/filter_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/http/header_validator.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"

#if defined(ENVOY_ENABLE_QUIC)
#include "source/common/quic/active_quic_listener.h"
#include "source/common/quic/quic_stat_names.h"
#endif

#include "source/common/listener_manager/active_raw_udp_listener_config.h"

#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

// TODO(mattklein123): A lot of code should be moved from this header file into the cc file.

namespace Envoy {

class FakeHttpConnection;
class FakeUpstream;

/**
 * Provides a fake HTTP stream for integration testing.
 */
class FakeStream : public Http::RequestDecoder,
                   public Http::StreamCallbacks,
                   Logger::Loggable<Logger::Id::testing> {
public:
  FakeStream(FakeHttpConnection& parent, Http::ResponseEncoder& encoder,
             Event::TestTimeSystem& time_system);

  uint64_t bodyLength() {
    absl::MutexLock lock(&lock_);
    return body_.length();
  }
  Buffer::Instance& body() {
    absl::MutexLock lock(&lock_);
    return body_;
  }
  bool complete() {
    absl::MutexLock lock(&lock_);
    return end_stream_;
  }

  // Execute a callback using the dispatcher associated with the FakeStream's connection. This
  // allows execution of non-interrupted sequences of operations on the fake stream which may run
  // into trouble if client-side events are interleaved.
  void postToConnectionThread(std::function<void()> cb);
  void encode1xxHeaders(const Http::ResponseHeaderMap& headers);
  void encodeHeaders(const Http::HeaderMap& headers, bool end_stream);
  void encodeData(uint64_t size, bool end_stream);
  void encodeData(Buffer::Instance& data, bool end_stream);
  void encodeData(std::string data, bool end_stream);
  void encodeTrailers(const Http::HeaderMap& trailers);
  void encodeResetStream();
  void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector);
  void readDisable(bool disable);
  const Http::RequestHeaderMap& headers() {
    absl::MutexLock lock(&lock_);
    return *headers_;
  }
  void setAddServedByHeader(bool add_header) { add_served_by_header_ = add_header; }
  const Http::RequestTrailerMapPtr& trailers() { return trailers_; }
  bool receivedData() { return received_data_; }
  Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() {
    return encoder_.http1StreamEncoderOptions();
  }
  void
  sendLocalReply(Http::Code code, absl::string_view body,
                 const std::function<void(Http::ResponseHeaderMap& headers)>& /*modify_headers*/,
                 const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                 absl::string_view /*details*/) override {
    bool is_head_request;
    {
      absl::MutexLock lock(&lock_);
      is_head_request = headers_ != nullptr &&
                        headers_->getMethodValue() == Http::Headers::get().MethodValues.Head;
    }
    Http::Utility::sendLocalReply(
        false,
        Http::Utility::EncodeFunctions(
            {nullptr, nullptr,
             [&](Http::ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
               encoder_.encodeHeaders(*headers, end_stream);
             },
             [&](Buffer::Instance& data, bool end_stream) -> void {
               encoder_.encodeData(data, end_stream);
             }}),
        Http::Utility::LocalReplyData({false, code, body, grpc_status, is_head_request}));
  }

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForHeadersComplete(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForData(Event::Dispatcher& client_dispatcher, uint64_t body_length,
              std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForData(Event::Dispatcher& client_dispatcher, absl::string_view body,
              std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  using ValidatorFunction = const std::function<bool(const std::string&)>;
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForData(Event::Dispatcher& client_dispatcher, const ValidatorFunction& data_validator,
              std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForEndStream(Event::Dispatcher& client_dispatcher,
                   std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForReset(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // gRPC convenience methods.
  void startGrpcStream(bool send_headers = true);
  void finishGrpcStream(Grpc::Status::GrpcStatus status);
  template <class T> void sendGrpcMessage(const T& message) {
    ASSERT(grpc_stream_started_,
           "start gRPC stream by calling startGrpcStream before sending a message");
    auto serialized_response = Grpc::Common::serializeToGrpcFrame(message);
    encodeData(*serialized_response, false);
    ENVOY_LOG(debug, "Sent gRPC message: {}", message.DebugString());
  }
  template <class T> void decodeGrpcFrame(T& message) {
    EXPECT_GE(decoded_grpc_frames_.size(), 1);
    if (decoded_grpc_frames_[0].length_ == 0) {
      decoded_grpc_frames_.erase(decoded_grpc_frames_.begin());
      return;
    }
    Buffer::ZeroCopyInputStreamImpl stream(std::move(decoded_grpc_frames_[0].data_));
    EXPECT_TRUE(decoded_grpc_frames_[0].flags_ == Grpc::GRPC_FH_DEFAULT);
    EXPECT_TRUE(message.ParseFromZeroCopyStream(&stream));
    ENVOY_LOG(debug, "Received gRPC message: {}", message.DebugString());
    decoded_grpc_frames_.erase(decoded_grpc_frames_.begin());
  }
  template <class T>
  ABSL_MUST_USE_RESULT testing::AssertionResult
  waitForGrpcMessage(Event::Dispatcher& client_dispatcher, T& message,
                     std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    Event::TestTimeSystem::RealTimeBound bound(timeout);
    ENVOY_LOG(debug, "Waiting for gRPC message...");
    if (!decoded_grpc_frames_.empty()) {
      decodeGrpcFrame(message);
      return AssertionSuccess();
    }
    if (!waitForData(client_dispatcher, 5, timeout)) {
      return testing::AssertionFailure() << "Timed out waiting for start of gRPC message.";
    }
    int last_body_size = 0;
    {
      absl::MutexLock lock(&lock_);
      last_body_size = body_.length();
      if (!grpc_decoder_.decode(body_, decoded_grpc_frames_)) {
        return testing::AssertionFailure()
               << "Couldn't decode gRPC data frame: " << body_.toString();
      }
    }
    if (decoded_grpc_frames_.empty()) {
      if (!waitForData(client_dispatcher, grpc_decoder_.length() - last_body_size,
                       bound.timeLeft())) {
        return testing::AssertionFailure() << "Timed out waiting for end of gRPC message.";
      }
      {
        absl::MutexLock lock(&lock_);
        if (!grpc_decoder_.decode(body_, decoded_grpc_frames_)) {
          return testing::AssertionFailure()
                 << "Couldn't decode gRPC data frame: " << body_.toString();
        }
      }
    }
    decodeGrpcFrame(message);
    return AssertionSuccess();
  }

  // Http::StreamDecoder
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr) override;

  // Http::RequestDecoder
  void decodeHeaders(Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::RequestTrailerMapPtr&& trailers) override;
  StreamInfo::StreamInfo& streamInfo() override {
    RELEASE_ASSERT(false, "initialize if this is needed");
    return *stream_info_;
  }
  std::list<AccessLog::InstanceSharedPtr> accessLogHandlers() override {
    return access_log_handlers_;
  }

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  virtual void setEndStream(bool end) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { end_stream_ = end; }

  Event::TestTimeSystem& timeSystem() { return time_system_; }

  Http::MetadataMap& metadataMap() { return metadata_map_; }
  absl::node_hash_map<std::string, uint64_t>& duplicatedMetadataKeyCount() {
    return duplicated_metadata_key_count_;
  }

protected:
  absl::Mutex lock_;
  Http::RequestHeaderMapSharedPtr headers_ ABSL_GUARDED_BY(lock_);
  Buffer::OwnedImpl body_ ABSL_GUARDED_BY(lock_);
  FakeHttpConnection& parent_;

private:
  Http::ResponseEncoder& encoder_;
  Http::RequestTrailerMapPtr trailers_ ABSL_GUARDED_BY(lock_);
  bool end_stream_ ABSL_GUARDED_BY(lock_){};
  bool saw_reset_ ABSL_GUARDED_BY(lock_){};
  Grpc::Decoder grpc_decoder_;
  std::vector<Grpc::Frame> decoded_grpc_frames_;
  bool add_served_by_header_{};
  Event::TestTimeSystem& time_system_;
  Http::MetadataMap metadata_map_;
  absl::node_hash_map<std::string, uint64_t> duplicated_metadata_key_count_;
  std::shared_ptr<StreamInfo::StreamInfo> stream_info_;
  std::list<AccessLog::InstanceSharedPtr> access_log_handlers_;
  bool received_data_{false};
  bool grpc_stream_started_{false};
  Http::ServerHeaderValidatorPtr header_validator_;
};

using FakeStreamPtr = std::unique_ptr<FakeStream>;

// Encapsulates various state and functionality related to sharing a Connection object across
// threads. With FakeUpstream fabricated objects, we have a Connection that is associated with a
// dispatcher on a thread managed by FakeUpstream. We want to be able to safely invoke methods on
// this object from other threads (e.g. the main test thread) and be able to track connection state
// (e.g. are we disconnected and the Connection is now possibly deleted). We manage this via a
// SharedConnectionWrapper that lives from when the Connection is added to the accepted connection
// queue and then through the lifetime of the Fake{Raw,Http}Connection that manages the Connection
// through active use.
class SharedConnectionWrapper : public Network::ConnectionCallbacks,
                                public LinkedObject<SharedConnectionWrapper> {
public:
  using DisconnectCallback = std::function<void()>;

  SharedConnectionWrapper(Network::Connection& connection)
      : connection_(connection), dispatcher_(connection_.dispatcher()) {
    connection_.addConnectionCallbacks(*this);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    // Throughout this entire function, we know that the connection_ cannot disappear, since this
    // callback is invoked prior to connection_ deferred delete. We also know by locking below,
    // that elsewhere where we also hold lock_, that the connection cannot disappear inside the
    // locked scope.
    absl::MutexLock lock(&lock_);
    if (event == Network::ConnectionEvent::RemoteClose ||
        event == Network::ConnectionEvent::LocalClose) {
      if (connection_.detectedCloseType() == Network::DetectedCloseType::RemoteReset ||
          connection_.detectedCloseType() == Network::DetectedCloseType::LocalReset) {
        rst_disconnected_ = true;
      }
      disconnected_ = true;
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  Event::Dispatcher& dispatcher() { return dispatcher_; }

  bool connected() {
    absl::MutexLock lock(&lock_);
    return connectedLockHeld();
  }

  bool connectedLockHeld() {
    lock_.AssertReaderHeld(); // TODO(mattklein123): This can't be annotated because the lock
                              // is acquired via the base connection reference. Fix this to
                              // remove the reference.
    return !disconnected_;
  }

  bool rstDisconnected() {
    lock_.AssertReaderHeld();
    return rst_disconnected_;
  }

  // This provides direct access to the underlying connection, but only to const methods.
  // Stateful connection related methods should happen on the connection's dispatcher via
  // executeOnDispatcher.
  // thread safety violations when crossing between the test thread and FakeUpstream thread.
  Network::Connection& connection() const { return connection_; }

  // Execute some function on the connection's dispatcher. This involves a cross-thread post and
  // wait-for-completion. If the connection is disconnected, either prior to post or when the
  // dispatcher schedules the callback, we silently ignore.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  executeOnDispatcher(std::function<void(Network::Connection&)> f,
                      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout,
                      bool allow_disconnects = true) {
    absl::MutexLock lock(&lock_);
    if (disconnected_) {
      return testing::AssertionSuccess();
    }
    // Sanity check: detect if the post and wait is attempted from the dispatcher thread; fail
    // immediately instead of deadlocking.
    ASSERT(!connection_.dispatcher().isThreadSafe(),
           "deadlock: executeOnDispatcher called from dispatcher thread.");
    bool callback_ready_event = false;
    bool unexpected_disconnect = false;
    connection_.dispatcher().post(
        [this, f, &lock = lock_, &callback_ready_event, &unexpected_disconnect]() -> void {
          // The use of connected() here, vs. !disconnected_, is because we want to use the lock_
          // acquisition to briefly serialize. This avoids us entering this completion and issuing
          // a notifyOne() until the wait() is ready to receive it below.
          if (connected()) {
            f(connection_);
          } else {
            unexpected_disconnect = true;
          }
          absl::MutexLock lock_guard(&lock);
          callback_ready_event = true;
        });
    Event::TestTimeSystem& time_system =
        dynamic_cast<Event::TestTimeSystem&>(connection_.dispatcher().timeSource());
    if (!time_system.waitFor(lock_, absl::Condition(&callback_ready_event), timeout)) {
      return testing::AssertionFailure() << "Timed out while executing on dispatcher.";
    }
    if (unexpected_disconnect && !allow_disconnects) {
      ENVOY_LOG_MISC(warn, "executeOnDispatcher failed due to disconnect\n");
    }
    return testing::AssertionSuccess();
  }

  absl::Mutex& lock() { return lock_; }

  void setParented() {
    absl::MutexLock lock(&lock_);
    ASSERT(!parented_);
    parented_ = true;
  }

private:
  Network::Connection& connection_;
  Event::Dispatcher& dispatcher_;
  absl::Mutex lock_;
  bool parented_ ABSL_GUARDED_BY(lock_){};
  bool disconnected_ ABSL_GUARDED_BY(lock_){};
  bool rst_disconnected_ ABSL_GUARDED_BY(lock_){};
};

using SharedConnectionWrapperPtr = std::unique_ptr<SharedConnectionWrapper>;

/**
 * Base class for both fake raw connections and fake HTTP connections.
 */
class FakeConnectionBase : public Logger::Loggable<Logger::Id::testing> {
public:
  virtual ~FakeConnectionBase() {
    absl::MutexLock lock(&lock_);
    ASSERT(initialized_);
    ASSERT(pending_cbs_ == 0);
  }

  ABSL_MUST_USE_RESULT
  testing::AssertionResult close(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult close(Network::ConnectionCloseType close_type,
                                 std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  readDisable(bool disable, std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForDisconnect(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForRstDisconnect(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForHalfClose(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  virtual void initialize() {
    absl::MutexLock lock(&lock_);
    initialized_ = true;
  }

  // Some upstream connection are supposed to be alive forever.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult virtual waitForNoPost(
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // The same caveats apply here as in SharedConnectionWrapper::connection().
  Network::Connection& connection() const { return shared_connection_.connection(); }
  bool connected() const { return shared_connection_.connected(); }

  void postToConnectionThread(std::function<void()> cb);
  SharedConnectionWrapper& sharedConnection() { return shared_connection_; }

protected:
  FakeConnectionBase(SharedConnectionWrapper& shared_connection, Event::TestTimeSystem& time_system)
      : shared_connection_(shared_connection), lock_(shared_connection.lock()),
        dispatcher_(shared_connection_.dispatcher()), time_system_(time_system) {}

  SharedConnectionWrapper& shared_connection_;
  absl::Mutex& lock_; // TODO(mattklein123): Use the shared connection lock and figure out better
                      // guarded by annotations.
  Event::Dispatcher& dispatcher_;
  bool initialized_ ABSL_GUARDED_BY(lock_){};
  bool half_closed_ ABSL_GUARDED_BY(lock_){};
  std::atomic<uint64_t> pending_cbs_{};
  Event::TestTimeSystem& time_system_;
};

/**
 * Provides a fake HTTP connection for integration testing.
 */
class FakeHttpConnection : public Http::ServerConnectionCallbacks, public FakeConnectionBase {
public:
  // This is a legacy alias.
  using Type = Envoy::Http::CodecType;
  static absl::string_view typeToString(Http::CodecType type) {
    switch (type) {
    case Http::CodecType::HTTP1:
      return "http1";
    case Http::CodecType::HTTP2:
      return "http2";
    case Http::CodecType::HTTP3:
      return "http3";
    }
    return "invalid";
  }

  FakeHttpConnection(FakeUpstream& fake_upstream, SharedConnectionWrapper& shared_connection,
                     Http::CodecType type, Event::TestTimeSystem& time_system,
                     uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
                     envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                         headers_with_underscores_action);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForNewStream(Event::Dispatcher& client_dispatcher, FakeStreamPtr& stream,
                   std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder, bool) override;
  // Should only be called for HTTP2 or above
  void onGoAway(Http::GoAwayErrorCode code) override;

  // Should only be called for HTTP2 or above, sends a GOAWAY frame with NO_ERROR.
  void encodeGoAway();

  // Should only be called for HTTP2 or above, sends a GOAWAY frame with ENHANCE_YOUR_CALM.
  void encodeProtocolError();

  // Update the maximum number of concurrent streams.
  void updateConcurrentStreams(uint64_t max_streams);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForInexactRawData(const char* data, std::string* out = nullptr,
                        std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  void writeRawData(absl::string_view data);
  ABSL_MUST_USE_RESULT AssertionResult postWriteRawData(std::string data);

  Http::ServerHeaderValidatorPtr makeHeaderValidator();

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(FakeHttpConnection& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      Http::Status status = parent_.codec_->dispatch(data);

      if (Http::isCodecProtocolError(status)) {
        ENVOY_LOG(debug, "FakeUpstream dispatch error: {}", status.message());
        // We don't do a full stream shutdown like HCM, but just shutdown the
        // connection for now.
        read_filter_callbacks_->connection().close(
            Network::ConnectionCloseType::FlushWriteAndDelay);
      }
      return Network::FilterStatus::StopIteration;
    }

    void
    initializeReadFilterCallbacks(Network::ReadFilterCallbacks& read_filter_callbacks) override {
      read_filter_callbacks_ = &read_filter_callbacks;
    }

    Network::ReadFilterCallbacks* read_filter_callbacks_{};
    FakeHttpConnection& parent_;
  };

  const Http::CodecType type_;
  Http::ServerConnectionPtr codec_;
  std::list<FakeStreamPtr> new_streams_ ABSL_GUARDED_BY(lock_);
  testing::NiceMock<Server::MockOverloadManager> overload_manager_;
  testing::NiceMock<Random::MockRandomGenerator> random_;
  testing::NiceMock<Http::MockHeaderValidatorStats> header_validator_stats_;
  Http::HeaderValidatorFactoryPtr header_validator_factory_;
};

using FakeHttpConnectionPtr = std::unique_ptr<FakeHttpConnection>;

/**
 * Fake raw connection for integration testing.
 */
class FakeRawConnection : public FakeConnectionBase {
public:
  FakeRawConnection(SharedConnectionWrapper& shared_connection, Event::TestTimeSystem& time_system)
      : FakeConnectionBase(shared_connection, time_system) {}
  using ValidatorFunction = const std::function<bool(const std::string&)>;
  ~FakeRawConnection() override;

  // Writes to data. If data is nullptr, discards the received data.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForData(uint64_t num_bytes, std::string* data = nullptr,
              std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Wait until data_validator returns true.
  // example usage:
  // std::string data;
  // ASSERT_TRUE(waitForData(FakeRawConnection::waitForInexactMatch("foo"), &data));
  // EXPECT_EQ(data, "foobar");
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForData(const ValidatorFunction& data_validator, std::string* data = nullptr,
              std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult write(const std::string& data, bool end_stream = false,
                                 std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  void initialize() override;

  // Creates a ValidatorFunction which returns true when data_to_wait_for is
  // contained in the incoming data string. Unlike many of Envoy waitFor functions,
  // it does not expect an exact match, simply the presence of data_to_wait_for.
  static ValidatorFunction waitForInexactMatch(const char* data_to_wait_for) {
    return [data_to_wait_for](const std::string& data) -> bool {
      return data.find(data_to_wait_for) != std::string::npos;
    };
  }

  // Creates a ValidatorFunction which returns true when data_to_wait_for
  // equals the incoming data string.
  static ValidatorFunction waitForMatch(const char* data_to_wait_for) {
    return [data_to_wait_for](const std::string& data) -> bool { return data == data_to_wait_for; };
  }

  // Creates a ValidatorFunction which returns true when data_to_wait_for is
  // contains at least bytes_read bytes.
  static ValidatorFunction waitForAtLeastBytes(uint32_t bytes) {
    return [bytes](const std::string& data) -> bool { return data.size() >= bytes; };
  }

  void clearData() {
    absl::MutexLock lock(&lock_);
    data_.clear();
  }

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(FakeRawConnection& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override;

    FakeRawConnection& parent_;
  };

  std::string data_ ABSL_GUARDED_BY(lock_);
  std::shared_ptr<Network::ReadFilter> read_filter_;
};

using FakeRawConnectionPtr = std::unique_ptr<FakeRawConnection>;

struct FakeUpstreamConfig {
  struct UdpConfig {
    absl::optional<uint64_t> max_rx_datagram_size_;
  };

  FakeUpstreamConfig(Event::TestTimeSystem& time_system) : time_system_(time_system) {
    http2_options_ = ::Envoy::Http2::Utility::initializeAndValidateOptions(http2_options_);
    // Legacy options which are always set.
    http2_options_.set_allow_connect(true);
    http2_options_.set_allow_metadata(true);
    http3_options_.set_allow_extended_connect(true);
  }

  Event::TestTimeSystem& time_system_;
  Http::CodecType upstream_protocol_{Http::CodecType::HTTP1};
  bool enable_half_close_{};
  absl::optional<UdpConfig> udp_fake_upstream_;
  envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  envoy::config::listener::v3::QuicProtocolOptions quic_options_;
  uint32_t max_request_headers_kb_ = Http::DEFAULT_MAX_REQUEST_HEADERS_KB;
  uint32_t max_request_headers_count_ = Http::DEFAULT_MAX_HEADERS_COUNT;
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::ALLOW;
};

/**
 * Provides a fake upstream server for integration testing.
 */
class FakeUpstream : Logger::Loggable<Logger::Id::testing>,
                     public Network::FilterChainManager,
                     public Network::FilterChainFactory {
public:
  // Creates a fake upstream bound to the specified unix domain socket path.
  FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
               const std::string& uds_path, const FakeUpstreamConfig& config);

  // Creates a fake upstream bound to the specified |address|.
  FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
               const Network::Address::InstanceConstSharedPtr& address,
               const FakeUpstreamConfig& config);

  // Creates a fake upstream bound to INADDR_ANY and the specified `port`.
  // Set `defer_initialization` to true if you want the FakeUpstream to not immediately listen for
  // incoming connections, and instead want to control when the FakeUpstream is available for
  // listening. If `defer_initialization` is set to true, call initializeServer() before invoking
  // any other functions in this class.
  FakeUpstream(uint32_t port, Network::Address::IpVersion version, const FakeUpstreamConfig& config,
               bool defer_initialization = false);

  FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
               uint32_t port, Network::Address::IpVersion version,
               const FakeUpstreamConfig& config);
  ~FakeUpstream() override;

  // Initializes the FakeUpstream's server.
  void initializeServer();

  // Returns true if the server has been initialized, i.e. that initializeServer() executed
  // successfully. Returns false otherwise.
  bool isInitialized() { return initialized_; }

  Http::CodecType httpType() { return http_type_; }

  // Returns the new connection via the connection argument.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForHttpConnection(Event::Dispatcher& client_dispatcher, FakeHttpConnectionPtr& connection,
                        std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult assertPendingConnectionsEmpty();

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForRawConnection(FakeRawConnectionPtr& connection,
                       std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  Network::Address::InstanceConstSharedPtr localAddress() const {
    return socket_->connectionInfoProvider().localAddress();
  }

  void convertFromRawToHttp(FakeRawConnectionPtr& raw_connection,
                            FakeHttpConnectionPtr& connection);

  virtual std::unique_ptr<FakeRawConnection>
  makeRawConnection(SharedConnectionWrapper& shared_connection,
                    Event::TestTimeSystem& time_system) {
    return std::make_unique<FakeRawConnection>(shared_connection, time_system);
  }

  // Wait for one of the upstreams to receive a connection
  ABSL_MUST_USE_RESULT
  static testing::AssertionResult
  waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                        std::vector<std::unique_ptr<FakeUpstream>>& upstreams,
                        FakeHttpConnectionPtr& connection,
                        std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Waits for 1 UDP datagram to be received.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForUdpDatagram(Network::UdpRecvData& data_to_fill,
                     std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Send a UDP datagram on the fake upstream thread.
  void sendUdpDatagram(const std::string& buffer,
                       const Network::Address::InstanceConstSharedPtr& peer);

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&,
                                              const StreamInfo::StreamInfo&) const override {
    return filter_chain_.get();
  }

  // Network::FilterChainFactory
  bool
  createNetworkFilterChain(Network::Connection& connection,
                           const Filter::NetworkFilterFactoriesList& filter_factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& listener) override;
  void createUdpListenerFilterChain(Network::UdpListenerFilterManager& udp_listener,
                                    Network::UdpReadFilterCallbacks& callbacks) override;
  bool createQuicListenerFilterChain(Network::QuicListenerFilterManager& listener) override;

  void setReadDisableOnNewConnection(bool value) { read_disable_on_new_connection_ = value; }
  void setDisableAllAndDoNotEnable(bool value) { disable_and_do_not_enable_ = value; }
  Event::TestTimeSystem& timeSystem() { return time_system_; }

  // Stops the dispatcher loop and joins the listening thread.
  void cleanUp();

  Http::Http1::CodecStats& http1CodecStats() {
    return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, *stats_scope_);
  }

  Http::Http2::CodecStats& http2CodecStats() {
    return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *stats_scope_);
  }

  Http::Http3::CodecStats& http3CodecStats() {
    return Http::Http3::CodecStats::atomicGet(http3_codec_stats_, *stats_scope_);
  }

  // Write into the outbound buffer of the network connection at the specified index.
  // Note: that this write bypasses any processing by the upstream codec.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  rawWriteConnection(uint32_t index, const std::string& data, bool end_stream = false,
                     std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  const envoy::config::core::v3::Http2ProtocolOptions& http2Options() { return http2_options_; }
  const envoy::config::core::v3::Http3ProtocolOptions& http3Options() { return http3_options_; }

  Event::DispatcherPtr& dispatcher() { return dispatcher_; }
  absl::Mutex& lock() { return lock_; }

protected:
  const FakeUpstreamConfig& config() const { return config_; }

  Stats::IsolatedStoreImpl stats_store_;
  const Http::CodecType http_type_;

private:
  FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
               Network::SocketPtr&& connection, const FakeUpstreamConfig& config,
               bool defer_initialization = false);

  class FakeListenSocketFactory : public Network::ListenSocketFactory {
  public:
    FakeListenSocketFactory(Network::SocketSharedPtr socket) : socket_(socket) {}

    // Network::ListenSocketFactory
    Network::Socket::Type socketType() const override { return socket_->socketType(); }
    const Network::Address::InstanceConstSharedPtr& localAddress() const override {
      return socket_->connectionInfoProvider().localAddress();
    }
    Network::SocketSharedPtr getListenSocket(uint32_t) override { return socket_; }
    Network::ListenSocketFactoryPtr clone() const override { return nullptr; }
    void closeAllSockets() override {}
    void doFinalPreWorkerInit() override;

  private:
    Network::SocketSharedPtr socket_;
  };

  class FakeUdpFilter : public Network::UdpListenerReadFilter {
  public:
    FakeUdpFilter(FakeUpstream& parent, Network::UdpReadFilterCallbacks& callbacks)
        : UdpListenerReadFilter(callbacks), parent_(parent) {}

    // Network::UdpListenerReadFilter
    Network::FilterStatus onData(Network::UdpRecvData& data) override {
      return parent_.onRecvDatagram(data);
    }
    Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
      PANIC("not implemented");
    }

  private:
    FakeUpstream& parent_;
  };

  class FakeListener : public Network::ListenerConfig {
  public:
    struct UdpListenerConfigImpl : public Network::UdpListenerConfig {
      UdpListenerConfigImpl()
          : writer_factory_(std::make_unique<Network::UdpDefaultWriterFactory>()),
            listener_worker_router_(1) {}

      // Network::UdpListenerConfig
      Network::ActiveUdpListenerFactory& listenerFactory() override { return *listener_factory_; }
      Network::UdpPacketWriterFactory& packetWriterFactory() override { return *writer_factory_; }
      Network::UdpListenerWorkerRouter&
      listenerWorkerRouter(const Network::Address::Instance&) override {
        return listener_worker_router_;
      }
      const envoy::config::listener::v3::UdpListenerConfig& config() override { return config_; }

      envoy::config::listener::v3::UdpListenerConfig config_;
      std::unique_ptr<Network::ActiveUdpListenerFactory> listener_factory_;
      std::unique_ptr<Network::UdpPacketWriterFactory> writer_factory_;
      Network::UdpListenerWorkerRouterImpl listener_worker_router_;
    };

    FakeListener(FakeUpstream& parent, bool is_quic = false)
        : parent_(parent), name_("fake_upstream"), init_manager_(nullptr) {
      if (is_quic) {
#if defined(ENVOY_ENABLE_QUIC)
        udp_listener_config_.listener_factory_ = std::make_unique<Quic::ActiveQuicListenerFactory>(
            parent_.quic_options_, 1, parent_.quic_stat_names_, parent_.validation_visitor_,
            absl::nullopt);
        // Initialize QUICHE flags.
        quiche::FlagRegistry::getInstance();
#else
        ASSERT(false, "Running a test that requires QUIC without compiling QUIC");
#endif
      } else {
        udp_listener_config_.listener_factory_ =
            std::make_unique<Server::ActiveRawUdpListenerFactory>(1);
      }
    }

    UdpListenerConfigImpl udp_listener_config_;

  private:
    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override { return parent_; }
    Network::FilterChainFactory& filterChainFactory() override { return parent_; }
    std::vector<Network::ListenSocketFactoryPtr>& listenSocketFactories() override {
      return parent_.socket_factories_;
    }
    bool bindToPort() const override { return true; }
    bool handOffRestoredDestinationConnections() const override { return false; }
    uint32_t perConnectionBufferLimitBytes() const override { return 0; }
    std::chrono::milliseconds listenerFiltersTimeout() const override { return {}; }
    bool continueOnListenerFiltersTimeout() const override { return false; }
    Stats::Scope& listenerScope() override { return *parent_.stats_store_.rootScope(); }
    uint64_t listenerTag() const override { return 0; }
    const std::string& name() const override { return name_; }
    Network::UdpListenerConfigOptRef udpListenerConfig() override { return udp_listener_config_; }
    Network::InternalListenerConfigOptRef internalListenerConfig() override { return {}; }
    Network::ConnectionBalancer& connectionBalancer(const Network::Address::Instance&) override {
      return connection_balancer_;
    }
    const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
      return empty_access_logs_;
    }
    const Network::ListenerInfo& listenerInfo() const override { return listener_info_; }
    ResourceLimit& openConnections() override { return connection_resource_; }
    uint32_t tcpBacklogSize() const override { return ENVOY_TCP_BACKLOG_SIZE; }
    uint32_t maxConnectionsToAcceptPerSocketEvent() const override {
      return Network::DefaultMaxConnectionsToAcceptPerSocketEvent;
    }
    Init::Manager& initManager() override { return *init_manager_; }
    bool ignoreGlobalConnLimit() const override { return false; }

    void setMaxConnections(const uint32_t num_connections) {
      connection_resource_.setMax(num_connections);
    }
    void clearMaxConnections() { connection_resource_.resetMax(); }

    FakeUpstream& parent_;
    const std::string name_;
    Network::NopConnectionBalancerImpl connection_balancer_;
    BasicResourceLimitImpl connection_resource_;
    const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
    std::unique_ptr<Init::Manager> init_manager_;
    testing::NiceMock<Network::MockListenerInfo> listener_info_;
  };

  void threadRoutine();
  SharedConnectionWrapper& consumeConnection() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  Network::FilterStatus onRecvDatagram(Network::UdpRecvData& data);
  AssertionResult
  runOnDispatcherThreadAndWait(std::function<AssertionResult()> cb,
                               std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  const envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  const envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  envoy::config::listener::v3::QuicProtocolOptions quic_options_;
  Network::SocketSharedPtr socket_;
  std::vector<Network::ListenSocketFactoryPtr> socket_factories_;
  ConditionalInitializer server_initialized_;
  // Guards any objects which can be altered both in the upstream thread and the
  // main test thread.
  absl::Mutex lock_;
  Thread::ThreadPtr thread_;
  Api::ApiPtr api_;
  Event::TestTimeSystem& time_system_;
  Event::DispatcherPtr dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  std::list<SharedConnectionWrapperPtr> new_connections_ ABSL_GUARDED_BY(lock_);
  testing::NiceMock<Runtime::MockLoader> runtime_;
  testing::NiceMock<Random::MockRandomGenerator> random_;

  // When a QueuedConnectionWrapper is popped from new_connections_, ownership is transferred to
  // consumed_connections_. This allows later the Connection destruction (when the FakeUpstream is
  // deleted) on the same thread that allocated the connection.
  std::list<SharedConnectionWrapperPtr> consumed_connections_ ABSL_GUARDED_BY(lock_);
  std::list<FakeHttpConnectionPtr> quic_connections_ ABSL_GUARDED_BY(lock_);
  const FakeUpstreamConfig config_;
  // Normally connections are read disabled until a fake raw or http connection
  // is created, and are then read enabled. Setting these true skips both these.
  bool read_disable_on_new_connection_;
  // Setting this true disables all events and does not re-enable as the above does.
  bool disable_and_do_not_enable_{};
  const bool enable_half_close_;
  FakeListener listener_;
  const Network::FilterChainSharedPtr filter_chain_;
  std::list<Network::UdpRecvData> received_datagrams_ ABSL_GUARDED_BY(lock_);
  Stats::ScopeSharedPtr stats_scope_;
  Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
  Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
  Http::Http3::CodecStats::AtomicPtr http3_codec_stats_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
#ifdef ENVOY_ENABLE_QUIC
  Quic::QuicStatNames quic_stat_names_ = Quic::QuicStatNames(stats_store_.symbolTable());
#endif
  bool initialized_ = false;
};

using FakeUpstreamPtr = std::unique_ptr<FakeUpstream>;

} // namespace Envoy
