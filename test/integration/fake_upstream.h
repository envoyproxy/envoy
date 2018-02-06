#pragma once

#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>

#include "envoy/api/api.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/server/configuration.h"
#include "envoy/server/listener_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/thread.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/network/filter_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/stats/stats_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

namespace Envoy {
class FakeHttpConnection;

/**
 * Provides a fake HTTP stream for integration testing.
 */
class FakeStream : public Http::StreamDecoder,
                   public Http::StreamCallbacks,
                   Logger::Loggable<Logger::Id::testing> {
public:
  FakeStream(FakeHttpConnection& parent, Http::StreamEncoder& encoder);

  uint64_t bodyLength() { return body_.length(); }
  Buffer::Instance& body() { return body_; }
  bool complete() { return end_stream_; }
  void encodeHeaders(const Http::HeaderMapImpl& headers, bool end_stream);
  void encodeData(uint64_t size, bool end_stream);
  void encodeData(Buffer::Instance& data, bool end_stream);
  void encodeTrailers(const Http::HeaderMapImpl& trailers);
  void encodeResetStream();
  const Http::HeaderMap& headers() { return *headers_; }
  void setAddServedByHeader(bool add_header) { add_served_by_header_ = add_header; }
  const Http::HeaderMapPtr& trailers() { return trailers_; }
  void waitForHeadersComplete();
  void waitForData(Event::Dispatcher& client_dispatcher, uint64_t body_length);
  void waitForEndStream(Event::Dispatcher& client_dispatcher);
  void waitForReset();

  // gRPC convenience methods.
  void startGrpcStream();
  void finishGrpcStream(Grpc::Status::GrpcStatus status);
  template <class T> void sendGrpcMessage(const T& message) {
    auto serialized_response = Grpc::Common::serializeBody(message);
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
  template <class T> void waitForGrpcMessage(Event::Dispatcher& client_dispatcher, T& message) {
    ENVOY_LOG(debug, "Waiting for gRPC message...");
    if (!decoded_grpc_frames_.empty()) {
      decodeGrpcFrame(message);
      return;
    }
    waitForData(client_dispatcher, 5);
    {
      std::unique_lock<std::mutex> lock(lock_);
      EXPECT_TRUE(grpc_decoder_.decode(body(), decoded_grpc_frames_));
    }
    if (decoded_grpc_frames_.size() < 1) {
      waitForData(client_dispatcher, grpc_decoder_.length());
      {
        std::unique_lock<std::mutex> lock(lock_);
        EXPECT_TRUE(grpc_decoder_.decode(body(), decoded_grpc_frames_));
      }
    }
    decodeGrpcFrame(message);
    ENVOY_LOG(debug, "Received gRPC message: {}", message.DebugString());
  }

  // Http::StreamDecoder
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  virtual void setEndStream(bool end) { end_stream_ = end; }

protected:
  Http::HeaderMapPtr headers_;

private:
  FakeHttpConnection& parent_;
  Http::StreamEncoder& encoder_;
  std::mutex lock_;
  std::condition_variable decoder_event_;
  Http::HeaderMapPtr trailers_;
  bool end_stream_{};
  Buffer::OwnedImpl body_;
  bool saw_reset_{};
  Grpc::Decoder grpc_decoder_;
  std::vector<Grpc::Frame> decoded_grpc_frames_;
  bool add_served_by_header_{};
};

typedef std::unique_ptr<FakeStream> FakeStreamPtr;

/**
 * Wraps a raw Network::Connection in a safe way, such that the connection can
 * be placed in a queue for an arbitrary amount of time. It handles disconnects
 * that take place in the queued state by failing the test. Once a
 * QueuedConnectionWrapper object is instantiated by FakeHttpConnection or
 * FakeRawConnection, it no longer plays a role.
 * TODO(htuch): We can simplify the storage lifetime by destructing if/when
 * removeConnectionCallbacks is added.
 */
class QueuedConnectionWrapper : public Network::ConnectionCallbacks {
public:
  QueuedConnectionWrapper(Network::Connection& connection, bool allow_unexpected_disconnects)
      : connection_(connection), parented_(false),
        allow_unexpected_disconnects_(allow_unexpected_disconnects) {
    connection_.addConnectionCallbacks(*this);
  }
  void set_parented() {
    std::unique_lock<std::mutex> lock(lock_);
    parented_ = true;
  }
  Network::Connection& connection() const { return connection_; }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    std::unique_lock<std::mutex> lock(lock_);
    RELEASE_ASSERT(parented_ || allow_unexpected_disconnects_ ||
                   (event != Network::ConnectionEvent::RemoteClose &&
                    event != Network::ConnectionEvent::LocalClose));
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Network::Connection& connection_;
  bool parented_;
  std::mutex lock_;
  bool allow_unexpected_disconnects_;
};

typedef std::unique_ptr<QueuedConnectionWrapper> QueuedConnectionWrapperPtr;

/**
 * Base class for both fake raw connections and fake HTTP connections.
 */
class FakeConnectionBase : public Network::ConnectionCallbacks {
public:
  ~FakeConnectionBase() { ASSERT(initialized_); }
  void close();
  void readDisable(bool disable);
  // By default waitForDisconnect assumes the next event is a disconnect and
  // fails an assert if an unexpected event occurs. If a caller truly wishes to
  // wait until disconnect, set ignore_spurious_events = true.
  void waitForDisconnect(bool ignore_spurious_events = false);

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void initialize() {
    initialized_ = true;
    connection_wrapper_->set_parented();
    connection_.dispatcher().post([this]() -> void { connection_.addConnectionCallbacks(*this); });
  }

protected:
  FakeConnectionBase(QueuedConnectionWrapperPtr connection_wrapper)
      : connection_(connection_wrapper->connection()),
        connection_wrapper_(std::move(connection_wrapper)) {}

  Network::Connection& connection_;
  std::mutex lock_;
  std::condition_variable connection_event_;
  bool disconnected_{};
  bool initialized_{false};

private:
  // We hold on to this as connection callbacks live for the entire life of the
  // connection.
  QueuedConnectionWrapperPtr connection_wrapper_;
};

/**
 * Provides a fake HTTP connection for integration testing.
 */
class FakeHttpConnection : public Http::ServerConnectionCallbacks, public FakeConnectionBase {
public:
  enum class Type { HTTP1, HTTP2 };

  FakeHttpConnection(QueuedConnectionWrapperPtr connection_wrapper, Stats::Store& store, Type type);
  Network::Connection& connection() { return connection_; }
  // By default waitForNewStream assumes the next event is a new stream and
  // fails an assert if an unexpected event occurs. If a caller truly wishes to
  // wait for a new stream, set ignore_spurious_events = true.
  FakeStreamPtr waitForNewStream(Event::Dispatcher& client_dispatcher,
                                 bool ignore_spurious_events = false);

  // Http::ServerConnectionCallbacks
  Http::StreamDecoder& newStream(Http::StreamEncoder& response_encoder) override;
  void onGoAway() override { NOT_IMPLEMENTED; }

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(FakeHttpConnection& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override {
      parent_.codec_->dispatch(data);
      return Network::FilterStatus::StopIteration;
    }

    FakeHttpConnection& parent_;
  };

  Http::ServerConnectionPtr codec_;
  std::list<FakeStreamPtr> new_streams_;
};

typedef std::unique_ptr<FakeHttpConnection> FakeHttpConnectionPtr;

/**
 * Fake raw connection for integration testing.
 */
class FakeRawConnection : Logger::Loggable<Logger::Id::testing>, public FakeConnectionBase {
public:
  FakeRawConnection(QueuedConnectionWrapperPtr connection_wrapper)
      : FakeConnectionBase(std::move(connection_wrapper)) {
    connection_.addReadFilter(Network::ReadFilterSharedPtr{new ReadFilter(*this)});
  }

  std::string waitForData(uint64_t num_bytes);
  void write(const std::string& data);

private:
  struct ReadFilter : public Network::ReadFilterBaseImpl {
    ReadFilter(FakeRawConnection& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override;

    FakeRawConnection& parent_;
  };

  std::string data_;
};

typedef std::unique_ptr<FakeRawConnection> FakeRawConnectionPtr;

/**
 * Provides a fake upstream server for integration testing.
 */
class FakeUpstream : Logger::Loggable<Logger::Id::testing>, public Network::FilterChainFactory {
public:
  FakeUpstream(const std::string& uds_path, FakeHttpConnection::Type type);
  FakeUpstream(uint32_t port, FakeHttpConnection::Type type, Network::Address::IpVersion version);
  FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory, uint32_t port,
               FakeHttpConnection::Type type, Network::Address::IpVersion version);
  ~FakeUpstream();

  FakeHttpConnection::Type httpType() { return http_type_; }
  FakeHttpConnectionPtr waitForHttpConnection(Event::Dispatcher& client_dispatcher);
  FakeRawConnectionPtr waitForRawConnection();
  Network::Address::InstanceConstSharedPtr localAddress() const { return socket_->localAddress(); }

  // Wait for one of the upstreams to receive a connection
  static FakeHttpConnectionPtr
  waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                        std::vector<std::unique_ptr<FakeUpstream>>& upstreams);

  // Network::FilterChainFactory
  bool createNetworkFilterChain(Network::Connection& connection) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& listener) override;
  void set_allow_unexpected_disconnects(bool value) { allow_unexpected_disconnects_ = value; }

protected:
  Stats::IsolatedStoreImpl stats_store_;
  const FakeHttpConnection::Type http_type_;
  void cleanUp();

private:
  FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
               Network::SocketPtr&& connection, FakeHttpConnection::Type type);

  class FakeListener : public Network::ListenerConfig {
  public:
    FakeListener(FakeUpstream& parent) : parent_(parent), name_("fake_upstream") {}

  private:
    // Network::ListenerConfig
    Network::FilterChainFactory& filterChainFactory() override { return parent_; }
    Network::Socket& socket() override { return *parent_.socket_; }
    Network::TransportSocketFactory& transportSocketFactory() override {
      return *parent_.transport_socket_factory_;
    }
    bool bindToPort() override { return true; }
    bool handOffRestoredDestinationConnections() const override { return false; }
    uint32_t perConnectionBufferLimitBytes() override { return 0; }
    Stats::Scope& listenerScope() override { return parent_.stats_store_; }
    uint64_t listenerTag() const override { return 0; }
    const std::string& name() const override { return name_; }

    FakeUpstream& parent_;
    std::string name_;
  };

  void threadRoutine();

  Network::TransportSocketFactoryPtr transport_socket_factory_;
  Network::SocketPtr socket_;
  ConditionalInitializer server_initialized_;
  // Guards any objects which can be altered both in the upstream thread and the
  // main test thread.
  std::mutex lock_;
  Thread::ThreadPtr thread_;
  std::condition_variable new_connection_event_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Network::ConnectionHandlerPtr handler_;
  std::list<QueuedConnectionWrapperPtr> new_connections_; // Guarded by lock_
  bool allow_unexpected_disconnects_;
  FakeListener listener_;
};
} // namespace Envoy
