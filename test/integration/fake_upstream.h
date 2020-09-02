#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/basic_resource_impl.h"
#include "common/common/callback_impl.h"
#include "common/common/linked_object.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/network/connection_balancer_impl.h"
#include "common/network/filter_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/udp_default_writer_config.h"
#include "common/stats/isolated_store_impl.h"

#include "server/active_raw_udp_listener_config.h"

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
  void encode100ContinueHeaders(const Http::ResponseHeaderMap& headers);
  void encodeHeaders(const Http::HeaderMap& headers, bool end_stream);
  void encodeData(uint64_t size, bool end_stream);
  void encodeData(Buffer::Instance& data, bool end_stream);
  void encodeData(absl::string_view data, bool end_stream);
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
  sendLocalReply(bool is_grpc_request, Http::Code code, absl::string_view body,
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
        Http::Utility::LocalReplyData({is_grpc_request, code, body, grpc_status, is_head_request}));
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

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForEndStream(Event::Dispatcher& client_dispatcher,
                   std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForReset(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // gRPC convenience methods.
  void startGrpcStream();
  void finishGrpcStream(Grpc::Status::GrpcStatus status);
  template <class T> void sendGrpcMessage(const T& message) {
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
    {
      absl::MutexLock lock(&lock_);
      if (!grpc_decoder_.decode(body_, decoded_grpc_frames_)) {
        return testing::AssertionFailure()
               << "Couldn't decode gRPC data frame: " << body_.toString();
      }
    }
    if (decoded_grpc_frames_.empty()) {
      if (!waitForData(client_dispatcher, grpc_decoder_.length(), bound.timeLeft())) {
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
    ENVOY_LOG(debug, "Received gRPC message: {}", message.DebugString());
    return AssertionSuccess();
  }

  // Http::StreamDecoder
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr) override;

  // Http::RequestDecoder
  void decodeHeaders(Http::RequestHeaderMapPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::RequestTrailerMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  virtual void setEndStream(bool end) EXCLUSIVE_LOCKS_REQUIRED(lock_) { end_stream_ = end; }

  Event::TestTimeSystem& timeSystem() { return time_system_; }

  Http::MetadataMap& metadataMap() { return metadata_map_; }
  absl::node_hash_map<std::string, uint64_t>& duplicatedMetadataKeyCount() {
    return duplicated_metadata_key_count_;
  }

protected:
  absl::Mutex lock_;
  Http::RequestHeaderMapPtr headers_ ABSL_GUARDED_BY(lock_);
  Buffer::OwnedImpl body_ ABSL_GUARDED_BY(lock_);

private:
  FakeHttpConnection& parent_;
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
  bool received_data_{false};
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

  SharedConnectionWrapper(Network::Connection& connection, bool allow_unexpected_disconnects)
      : connection_(connection), allow_unexpected_disconnects_(allow_unexpected_disconnects) {
    connection_.addConnectionCallbacks(*this);
    addDisconnectCallback([this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
      RELEASE_ASSERT(parented_ || allow_unexpected_disconnects_,
                     "An queued upstream connection was torn down without being associated "
                     "with a fake connection. Either manage the connection via "
                     "waitForRawConnection() or waitForHttpConnection(), or "
                     "set_allow_unexpected_disconnects(true).\n See "
                     "https://github.com/envoyproxy/envoy/blob/master/test/integration/README.md#"
                     "unparented-upstream-connections");
    });
  }

  Common::CallbackHandle* addDisconnectCallback(DisconnectCallback callback) {
    absl::MutexLock lock(&lock_);
    return disconnect_callback_manager_.add(callback);
  }

  // Avoid directly removing by caller, since CallbackManager is not thread safe.
  void removeDisconnectCallback(Common::CallbackHandle* handle) {
    absl::MutexLock lock(&lock_);
    handle->remove();
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
      disconnected_ = true;
      disconnect_callback_manager_.runCallbacks();
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

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

  // This provides direct access to the underlying connection, but only to const methods.
  // Stateful connection related methods should happen on the connection's dispatcher via
  // executeOnDispatcher.
  // thread safety violations when crossing between the test thread and FakeUpstream thread.
  Network::Connection& connection() const { return connection_; }

  // Execute some function on the connection's dispatcher. This involves a cross-thread post and
  // wait-for-completion. If the connection is disconnected, either prior to post or when the
  // dispatcher schedules the callback, we silently ignore if allow_unexpected_disconnects_
  // is set.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  executeOnDispatcher(std::function<void(Network::Connection&)> f,
                      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    absl::MutexLock lock(&lock_);
    if (disconnected_) {
      return testing::AssertionSuccess();
    }
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
    if (unexpected_disconnect && !allow_unexpected_disconnects_) {
      return testing::AssertionFailure()
             << "The connection disconnected unexpectedly, and allow_unexpected_disconnects_ is "
                "false."
                "\n See "
                "https://github.com/envoyproxy/envoy/blob/master/test/integration/README.md#"
                "unexpected-disconnects";
    }
    return testing::AssertionSuccess();
  }

  absl::Mutex& lock() { return lock_; }

  void setParented() {
    absl::MutexLock lock(&lock_);
    parented_ = true;
  }

private:
  Network::Connection& connection_;
  absl::Mutex lock_;
  Common::CallbackManager<> disconnect_callback_manager_ ABSL_GUARDED_BY(lock_);
  bool parented_ ABSL_GUARDED_BY(lock_){};
  bool disconnected_ ABSL_GUARDED_BY(lock_){};
  const bool allow_unexpected_disconnects_;
};

using SharedConnectionWrapperPtr = std::unique_ptr<SharedConnectionWrapper>;

/**
 * Base class for both fake raw connections and fake HTTP connections.
 */
class FakeConnectionBase : public Logger::Loggable<Logger::Id::testing> {
public:
  virtual ~FakeConnectionBase() { ASSERT(initialized_); }

  ABSL_MUST_USE_RESULT
  testing::AssertionResult close(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  readDisable(bool disable, std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForDisconnect(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForHalfClose(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  ABSL_MUST_USE_RESULT
  virtual testing::AssertionResult initialize() {
    initialized_ = true;
    return testing::AssertionSuccess();
  }
  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  enableHalfClose(bool enabled, std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  // The same caveats apply here as in SharedConnectionWrapper::connection().
  Network::Connection& connection() const { return shared_connection_.connection(); }
  bool connected() const { return shared_connection_.connected(); }

protected:
  FakeConnectionBase(SharedConnectionWrapper& shared_connection, Event::TestTimeSystem& time_system)
      : shared_connection_(shared_connection), lock_(shared_connection.lock()),
        time_system_(time_system) {}

  SharedConnectionWrapper& shared_connection_;
  bool initialized_{};
  absl::Mutex& lock_; // TODO(mattklein123): Use the shared connection lock and figure out better
                      // guarded by annotations.
  bool half_closed_ ABSL_GUARDED_BY(lock_){};
  Event::TestTimeSystem& time_system_;
};

/**
 * Provides a fake HTTP connection for integration testing.
 */
class FakeHttpConnection : public Http::ServerConnectionCallbacks, public FakeConnectionBase {
public:
  enum class Type { HTTP1, HTTP2 };

  FakeHttpConnection(FakeUpstream& fake_upstream, SharedConnectionWrapper& shared_connection,
                     Type type, Event::TestTimeSystem& time_system, uint32_t max_request_headers_kb,
                     uint32_t max_request_headers_count,
                     envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                         headers_with_underscores_action);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForNewStream(Event::Dispatcher& client_dispatcher, FakeStreamPtr& stream,
                   std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder, bool) override;
  // Should only be called for HTTP2
  void onGoAway(Http::GoAwayErrorCode code) override;

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

  const Type type_;
  Http::ServerConnectionPtr codec_;
  std::list<FakeStreamPtr> new_streams_ ABSL_GUARDED_BY(lock_);
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

  ABSL_MUST_USE_RESULT
  testing::AssertionResult initialize() override {
    testing::AssertionResult result =
        shared_connection_.executeOnDispatcher([this](Network::Connection& connection) {
          connection.addReadFilter(Network::ReadFilterSharedPtr{new ReadFilter(*this)});
        });
    if (!result) {
      return result;
    }
    return FakeConnectionBase::initialize();
  }

  // Creates a ValidatorFunction which returns true when data_to_wait_for is
  // contained in the incoming data string. Unlike many of Envoy waitFor functions,
  // it does not expect an exact match, simply the presence of data_to_wait_for.
  static ValidatorFunction waitForInexactMatch(const char* data_to_wait_for) {
    return [data_to_wait_for](const std::string& data) -> bool {
      return data.find(data_to_wait_for) != std::string::npos;
    };
  }

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(FakeRawConnection& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override;

    FakeRawConnection& parent_;
  };

  std::string data_ ABSL_GUARDED_BY(lock_);
};

using FakeRawConnectionPtr = std::unique_ptr<FakeRawConnection>;

/**
 * Provides a fake upstream server for integration testing.
 */
class FakeUpstream : Logger::Loggable<Logger::Id::testing>,
                     public Network::FilterChainManager,
                     public Network::FilterChainFactory {
public:
  // Creates a fake upstream bound to the specified unix domain socket path.
  FakeUpstream(const std::string& uds_path, FakeHttpConnection::Type type,
               Event::TestTimeSystem& time_system);
  // Creates a fake upstream bound to the specified |address|.
  FakeUpstream(const Network::Address::InstanceConstSharedPtr& address,
               FakeHttpConnection::Type type, Event::TestTimeSystem& time_system,
               bool enable_half_close = false, bool udp_fake_upstream = false);

  // Creates a fake upstream bound to INADDR_ANY and the specified |port|.
  FakeUpstream(uint32_t port, FakeHttpConnection::Type type, Network::Address::IpVersion version,
               Event::TestTimeSystem& time_system, bool enable_half_close = false);
  FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory, uint32_t port,
               FakeHttpConnection::Type type, Network::Address::IpVersion version,
               Event::TestTimeSystem& time_system);
  ~FakeUpstream() override;

  FakeHttpConnection::Type httpType() { return http_type_; }

  // Returns the new connection via the connection argument.
  ABSL_MUST_USE_RESULT
  testing::AssertionResult waitForHttpConnection(
      Event::Dispatcher& client_dispatcher, FakeHttpConnectionPtr& connection,
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout,
      uint32_t max_request_headers_kb = Http::DEFAULT_MAX_REQUEST_HEADERS_KB,
      uint32_t max_request_headers_count = Http::DEFAULT_MAX_HEADERS_COUNT,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action = envoy::config::core::v3::HttpProtocolOptions::ALLOW);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult
  waitForRawConnection(FakeRawConnectionPtr& connection,
                       std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  Network::Address::InstanceConstSharedPtr localAddress() const { return socket_->localAddress(); }

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
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return filter_chain_.get();
  }

  // Network::FilterChainFactory
  bool
  createNetworkFilterChain(Network::Connection& connection,
                           const std::vector<Network::FilterFactoryCb>& filter_factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& listener) override;
  void createUdpListenerFilterChain(Network::UdpListenerFilterManager& udp_listener,
                                    Network::UdpReadFilterCallbacks& callbacks) override;

  void set_allow_unexpected_disconnects(bool value) { allow_unexpected_disconnects_ = value; }
  void setReadDisableOnNewConnection(bool value) { read_disable_on_new_connection_ = value; }
  Event::TestTimeSystem& timeSystem() { return time_system_; }

  // Stops the dispatcher loop and joins the listening thread.
  void cleanUp();

  Http::Http1::CodecStats& http1CodecStats() {
    return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, stats_store_);
  }

  Http::Http2::CodecStats& http2CodecStats() {
    return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, stats_store_);
  }

protected:
  Stats::IsolatedStoreImpl stats_store_;
  const FakeHttpConnection::Type http_type_;

private:
  FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
               Network::SocketPtr&& connection, FakeHttpConnection::Type type,
               Event::TestTimeSystem& time_system, bool enable_half_close);

  class FakeListenSocketFactory : public Network::ListenSocketFactory {
  public:
    FakeListenSocketFactory(Network::SocketSharedPtr socket) : socket_(socket) {}

    // Network::ListenSocketFactory
    Network::Socket::Type socketType() const override { return socket_->socketType(); }

    const Network::Address::InstanceConstSharedPtr& localAddress() const override {
      return socket_->localAddress();
    }

    Network::SocketSharedPtr getListenSocket() override { return socket_; }
    Network::SocketOptRef sharedSocket() const override { return *socket_; }

  private:
    Network::SocketSharedPtr socket_;
  };

  class FakeUpstreamUdpFilter : public Network::UdpListenerReadFilter {
  public:
    FakeUpstreamUdpFilter(FakeUpstream& parent, Network::UdpReadFilterCallbacks& callbacks)
        : UdpListenerReadFilter(callbacks), parent_(parent) {}

    // Network::UdpListenerReadFilter
    void onData(Network::UdpRecvData& data) override { parent_.onRecvDatagram(data); }
    void onReceiveError(Api::IoError::IoErrorCode) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  private:
    FakeUpstream& parent_;
  };

  class FakeListener : public Network::ListenerConfig {
  public:
    FakeListener(FakeUpstream& parent)
        : parent_(parent), name_("fake_upstream"),
          udp_listener_factory_(std::make_unique<Server::ActiveRawUdpListenerFactory>()),
          udp_writer_factory_(std::make_unique<Network::UdpDefaultWriterFactory>()) {}

  private:
    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override { return parent_; }
    Network::FilterChainFactory& filterChainFactory() override { return parent_; }
    Network::ListenSocketFactory& listenSocketFactory() override {
      return *parent_.socket_factory_;
    }
    bool bindToPort() override { return true; }
    bool handOffRestoredDestinationConnections() const override { return false; }
    uint32_t perConnectionBufferLimitBytes() const override { return 0; }
    std::chrono::milliseconds listenerFiltersTimeout() const override { return {}; }
    bool continueOnListenerFiltersTimeout() const override { return false; }
    Stats::Scope& listenerScope() override { return parent_.stats_store_; }
    uint64_t listenerTag() const override { return 0; }
    const std::string& name() const override { return name_; }
    Network::ActiveUdpListenerFactory* udpListenerFactory() override {
      return udp_listener_factory_.get();
    }
    Network::UdpPacketWriterFactoryOptRef udpPacketWriterFactory() override {
      return Network::UdpPacketWriterFactoryOptRef(std::ref(*udp_writer_factory_));
    }
    Network::ConnectionBalancer& connectionBalancer() override { return connection_balancer_; }
    envoy::config::core::v3::TrafficDirection direction() const override {
      return envoy::config::core::v3::UNSPECIFIED;
    }
    const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
      return empty_access_logs_;
    }
    ResourceLimit& openConnections() override { return connection_resource_; }
    uint32_t tcpBacklogSize() const override { return ENVOY_TCP_BACKLOG_SIZE; }

    void setMaxConnections(const uint32_t num_connections) {
      connection_resource_.setMax(num_connections);
    }
    void clearMaxConnections() { connection_resource_.resetMax(); }

    FakeUpstream& parent_;
    const std::string name_;
    Network::NopConnectionBalancerImpl connection_balancer_;
    const Network::ActiveUdpListenerFactoryPtr udp_listener_factory_;
    const Network::UdpPacketWriterFactoryPtr udp_writer_factory_;
    BasicResourceLimitImpl connection_resource_;
    const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
  };

  void threadRoutine();
  SharedConnectionWrapper& consumeConnection() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void onRecvDatagram(Network::UdpRecvData& data);

  Network::SocketSharedPtr socket_;
  Network::ListenSocketFactorySharedPtr socket_factory_;
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
  // When a QueuedConnectionWrapper is popped from new_connections_, ownership is transferred to
  // consumed_connections_. This allows later the Connection destruction (when the FakeUpstream is
  // deleted) on the same thread that allocated the connection.
  std::list<SharedConnectionWrapperPtr> consumed_connections_ ABSL_GUARDED_BY(lock_);
  bool allow_unexpected_disconnects_;
  bool read_disable_on_new_connection_;
  const bool enable_half_close_;
  FakeListener listener_;
  const Network::FilterChainSharedPtr filter_chain_;
  std::list<Network::UdpRecvData> received_datagrams_ ABSL_GUARDED_BY(lock_);
  Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
  Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
};

using FakeUpstreamPtr = std::unique_ptr<FakeUpstream>;

} // namespace Envoy
