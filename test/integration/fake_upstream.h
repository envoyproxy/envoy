#pragma once

#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/configuration.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/thread.h"
#include "common/network/filter_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/stats/stats_impl.h"
#include "server/connection_handler_impl.h"

#include "test/test_common/utility.h"

class FakeHttpConnection;

/**
 * Provides a fake HTTP stream for integration testing.
 */
class FakeStream : public Http::StreamDecoder, public Http::StreamCallbacks {
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
  const Http::HeaderMapPtr& trailers() { return trailers_; }
  void waitForHeadersComplete();
  void waitForData(Event::Dispatcher& client_dispatcher, uint64_t body_length);
  void waitForEndStream(Event::Dispatcher& client_dispatcher);
  void waitForReset();

  // Http::StreamDecoder
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;

private:
  FakeHttpConnection& parent_;
  Http::StreamEncoder& encoder_;
  std::mutex lock_;
  std::condition_variable decoder_event_;
  Http::HeaderMapPtr headers_;
  Http::HeaderMapPtr trailers_;
  bool end_stream_{};
  Buffer::OwnedImpl body_;
  bool saw_reset_{};
};

typedef std::unique_ptr<FakeStream> FakeStreamPtr;

/**
 * Base class for both fake raw connections and fake HTTP connections.
 */
class FakeConnectionBase : public Network::ConnectionCallbacks {
public:
  void close();
  void readDisable(bool disable);
  void waitForDisconnect();

  // Network::ConnectionCallbacks
  void onEvent(uint32_t events) override;

protected:
  FakeConnectionBase(Network::Connection& connection) : connection_(connection) {
    connection.addConnectionCallbacks(*this);
  }

  Network::Connection& connection_;
  std::mutex lock_;
  std::condition_variable connection_event_;
  bool disconnected_{};
};

/**
 * Provides a fake HTTP connection for integration testing.
 */
class FakeHttpConnection : public Http::ServerConnectionCallbacks, public FakeConnectionBase {
public:
  enum class Type { HTTP1, HTTP2 };

  FakeHttpConnection(Network::Connection& connection, Stats::Store& store, Type type);
  Network::Connection& connection() { return connection_; }
  FakeStreamPtr waitForNewStream();

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
  FakeRawConnection(Network::Connection& connection) : FakeConnectionBase(connection) {
    connection.addReadFilter(Network::ReadFilterSharedPtr{new ReadFilter(*this)});
  }

  void waitForData(uint64_t num_bytes);
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
  FakeUpstream(uint32_t port, FakeHttpConnection::Type type);
  FakeUpstream(Ssl::ServerContext* ssl_ctx, uint32_t port, FakeHttpConnection::Type type);
  ~FakeUpstream();

  FakeHttpConnection::Type httpType() { return http_type_; }
  FakeHttpConnectionPtr waitForHttpConnection(Event::Dispatcher& client_dispatcher);
  FakeRawConnectionPtr waitForRawConnection();

  // Network::FilterChainFactory
  bool createFilterChain(Network::Connection& connection) override;

  uint32_t port() const { return socket_->localAddress()->ip()->port(); }

private:
  FakeUpstream(Ssl::ServerContext* ssl_ctx, Network::ListenSocketPtr&& connection,
               FakeHttpConnection::Type type);
  void threadRoutine();

  Ssl::ServerContext* ssl_ctx_{};
  Network::ListenSocketPtr socket_;
  ConditionalInitializer server_initialized_;
  Thread::ThreadPtr thread_;
  std::mutex lock_;
  std::condition_variable new_connection_event_;
  Stats::IsolatedStoreImpl stats_store_;
  Server::ConnectionHandlerImpl handler_;
  std::list<Network::Connection*> new_connections_;
  FakeHttpConnection::Type http_type_;
};
