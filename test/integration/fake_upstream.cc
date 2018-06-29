#include "fake_upstream.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/fmt.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/ssl/ssl_socket.h"

#include "server/connection_handler_impl.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

namespace Envoy {
FakeStream::FakeStream(FakeHttpConnection& parent, Http::StreamEncoder& encoder)
    : parent_(parent), encoder_(encoder) {
  encoder.getStream().addCallbacks(*this);
}

void FakeStream::decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) {
  Thread::LockGuard lock(lock_);
  headers_ = std::move(headers);
  setEndStream(end_stream);
  decoder_event_.notifyOne();
}

void FakeStream::decodeData(Buffer::Instance& data, bool end_stream) {
  Thread::LockGuard lock(lock_);
  body_.add(data);
  setEndStream(end_stream);
  decoder_event_.notifyOne();
}

void FakeStream::decodeTrailers(Http::HeaderMapPtr&& trailers) {
  Thread::LockGuard lock(lock_);
  setEndStream(true);
  trailers_ = std::move(trailers);
  decoder_event_.notifyOne();
}

void FakeStream::encode100ContinueHeaders(const Http::HeaderMapImpl& headers) {
  std::shared_ptr<Http::HeaderMapImpl> headers_copy(
      new Http::HeaderMapImpl(static_cast<const Http::HeaderMap&>(headers)));
  parent_.connection().dispatcher().post(
      [this, headers_copy]() -> void { encoder_.encode100ContinueHeaders(*headers_copy); });
}

void FakeStream::encodeHeaders(const Http::HeaderMapImpl& headers, bool end_stream) {
  std::shared_ptr<Http::HeaderMapImpl> headers_copy(
      new Http::HeaderMapImpl(static_cast<const Http::HeaderMap&>(headers)));
  if (add_served_by_header_) {
    headers_copy->addCopy(Http::LowerCaseString("x-served-by"),
                          parent_.connection().localAddress()->asString());
  }
  parent_.connection().dispatcher().post([this, headers_copy, end_stream]() -> void {
    encoder_.encodeHeaders(*headers_copy, end_stream);
  });
}

void FakeStream::encodeData(uint64_t size, bool end_stream) {
  parent_.connection().dispatcher().post([this, size, end_stream]() -> void {
    Buffer::OwnedImpl data(std::string(size, 'a'));
    encoder_.encodeData(data, end_stream);
  });
}

void FakeStream::encodeData(Buffer::Instance& data, bool end_stream) {
  std::shared_ptr<Buffer::Instance> data_copy(new Buffer::OwnedImpl(data));
  parent_.connection().dispatcher().post(
      [this, data_copy, end_stream]() -> void { encoder_.encodeData(*data_copy, end_stream); });
}

void FakeStream::encodeTrailers(const Http::HeaderMapImpl& trailers) {
  std::shared_ptr<Http::HeaderMapImpl> trailers_copy(
      new Http::HeaderMapImpl(static_cast<const Http::HeaderMap&>(trailers)));
  parent_.connection().dispatcher().post(
      [this, trailers_copy]() -> void { encoder_.encodeTrailers(*trailers_copy); });
}

void FakeStream::encodeResetStream() {
  parent_.connection().dispatcher().post(
      [this]() -> void { encoder_.getStream().resetStream(Http::StreamResetReason::LocalReset); });
}

void FakeStream::onResetStream(Http::StreamResetReason) {
  Thread::LockGuard lock(lock_);
  saw_reset_ = true;
  decoder_event_.notifyOne();
}

void FakeStream::waitForHeadersComplete() {
  Thread::LockGuard lock(lock_);
  while (!headers_) {
    decoder_event_.wait(lock_);
  }
}

void FakeStream::waitForData(Event::Dispatcher& client_dispatcher, uint64_t body_length) {
  Thread::LockGuard lock(lock_);
  while (bodyLength() < body_length) {
    decoder_event_.waitFor(lock_, std::chrono::milliseconds(5));
    if (bodyLength() < body_length) {
      // Run the client dispatcher since we may need to process window updates, etc.
      client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
    }
  }
}

void FakeStream::waitForEndStream(Event::Dispatcher& client_dispatcher) {
  Thread::LockGuard lock(lock_);
  while (!end_stream_) {
    decoder_event_.waitFor(lock_, std::chrono::milliseconds(5));
    if (!end_stream_) {
      // Run the client dispatcher since we may need to process window updates, etc.
      client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
    }
  }
}

void FakeStream::waitForReset() {
  Thread::LockGuard lock(lock_);
  while (!saw_reset_) {
    decoder_event_.wait(lock_); // Safe since CondVar::wait won't throw.
  }
}

void FakeStream::startGrpcStream() {
  encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
}

void FakeStream::finishGrpcStream(Grpc::Status::GrpcStatus status) {
  encodeTrailers(
      Http::TestHeaderMapImpl{{"grpc-status", std::to_string(static_cast<uint32_t>(status))}});
}

FakeHttpConnection::FakeHttpConnection(SharedConnectionWrapper& shared_connection,
                                       Stats::Store& store, Type type)
    : FakeConnectionBase(shared_connection) {
  if (type == Type::HTTP1) {
    codec_.reset(new Http::Http1::ServerConnectionImpl(shared_connection_.connection(), *this,
                                                       Http::Http1Settings()));
  } else {
    codec_.reset(new Http::Http2::ServerConnectionImpl(shared_connection_.connection(), *this,
                                                       store, Http::Http2Settings()));
    ASSERT(type == Type::HTTP2);
  }

  shared_connection_.connection().addReadFilter(
      Network::ReadFilterSharedPtr{new ReadFilter(*this)});
}

void FakeConnectionBase::close() {
  shared_connection_.executeOnDispatcher([](Network::Connection& connection) {
    connection.close(Network::ConnectionCloseType::FlushWrite);
  });
}

void FakeConnectionBase::readDisable(bool disable) {
  shared_connection_.executeOnDispatcher(
      [disable](Network::Connection& connection) { connection.readDisable(disable); });
}

void FakeConnectionBase::enableHalfClose(bool enable) {
  shared_connection_.executeOnDispatcher(
      [enable](Network::Connection& connection) { connection.enableHalfClose(enable); });
}

Http::StreamDecoder& FakeHttpConnection::newStream(Http::StreamEncoder& encoder) {
  Thread::LockGuard lock(lock_);
  new_streams_.emplace_back(new FakeStream(*this, encoder));
  connection_event_.notifyOne();
  return *new_streams_.back();
}

void FakeConnectionBase::waitForDisconnect(bool ignore_spurious_events) {
  Thread::LockGuard lock(lock_);
  while (shared_connection_.connected()) {
    connection_event_.wait(lock_); // Safe since CondVar::wait won't throw.
    // The default behavior of waitForDisconnect is to assume the test cleanly
    // calls waitForData, waitForNewStream, etc. to handle all events on the
    // connection. If the caller explicitly notes that other events should be
    // ignored, continue looping until a disconnect is detected. Otherwise fall
    // through and hit the assert below.
    if (!ignore_spurious_events) {
      break;
    }
  }

  ASSERT(!shared_connection_.connected());
}

void FakeConnectionBase::waitForHalfClose(bool ignore_spurious_events) {
  Thread::LockGuard lock(lock_);
  while (!half_closed_) {
    connection_event_.wait(lock_); // Safe since CondVar::wait won't throw.
    // The default behavior of waitForHalfClose is to assume the test cleanly
    // calls waitForData, waitForNewStream, etc. to handle all events on the
    // connection. If the caller explicitly notes that other events should be
    // ignored, continue looping until a disconnect is detected. Otherwise fall
    // through and hit the assert below.
    if (!ignore_spurious_events) {
      break;
    }
  }

  ASSERT(half_closed_);
}

FakeStreamPtr FakeHttpConnection::waitForNewStream(Event::Dispatcher& client_dispatcher,
                                                   bool ignore_spurious_events) {
  Thread::LockGuard lock(lock_);
  while (new_streams_.empty()) {
    Thread::CondVar::WaitStatus status =
        connection_event_.waitFor(lock_, std::chrono::milliseconds(5));
    // As with waitForDisconnect, by default, waitForNewStream returns after the next event.
    // If the caller explicitly notes other events should be ignored, it will instead actually
    // wait for the next new stream, ignoring other events such as onData()
    if (status == Thread::CondVar::WaitStatus::NoTimeout && !ignore_spurious_events) {
      break;
    }
    if (new_streams_.empty()) {
      // Run the client dispatcher since we may need to process window updates, etc.
      client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  ASSERT(!new_streams_.empty());
  FakeStreamPtr stream = std::move(new_streams_.front());
  new_streams_.pop_front();
  return stream;
}

FakeUpstream::FakeUpstream(const std::string& uds_path, FakeHttpConnection::Type type)
    : FakeUpstream(Network::Test::createRawBufferSocketFactory(),
                   Network::SocketPtr{new Network::UdsListenSocket(
                       std::make_shared<Network::Address::PipeInstance>(uds_path))},
                   type, false) {
  ENVOY_LOG(info, "starting fake server on unix domain socket {}", uds_path);
}

static Network::SocketPtr makeTcpListenSocket(uint32_t port, Network::Address::IpVersion version) {
  return Network::SocketPtr{new Network::TcpListenSocket(
      Network::Utility::parseInternetAddressAndPort(
          fmt::format("{}:{}", Network::Test::getAnyAddressUrlString(version), port)),
      nullptr, true)};
}

FakeUpstream::FakeUpstream(uint32_t port, FakeHttpConnection::Type type,
                           Network::Address::IpVersion version, bool enable_half_close)
    : FakeUpstream(Network::Test::createRawBufferSocketFactory(),
                   makeTcpListenSocket(port, version), type, enable_half_close) {
  ENVOY_LOG(info, "starting fake server on port {}. Address version is {}",
            this->localAddress()->ip()->port(), Network::Test::addressVersionAsString(version));
}

FakeUpstream::FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                           uint32_t port, FakeHttpConnection::Type type,
                           Network::Address::IpVersion version)
    : FakeUpstream(std::move(transport_socket_factory), makeTcpListenSocket(port, version), type,
                   false) {
  ENVOY_LOG(info, "starting fake SSL server on port {}. Address version is {}",
            this->localAddress()->ip()->port(), Network::Test::addressVersionAsString(version));
}

FakeUpstream::FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                           Network::SocketPtr&& listen_socket, FakeHttpConnection::Type type,
                           bool enable_half_close)
    : http_type_(type), socket_(std::move(listen_socket)),
      api_(new Api::Impl(std::chrono::milliseconds(10000))),
      dispatcher_(api_->allocateDispatcher()),
      handler_(new Server::ConnectionHandlerImpl(ENVOY_LOGGER(), *dispatcher_)),
      allow_unexpected_disconnects_(false), enable_half_close_(enable_half_close), listener_(*this),
      filter_chain_(Network::Test::createEmptyFilterChain(std::move(transport_socket_factory))) {
  thread_.reset(new Thread::Thread([this]() -> void { threadRoutine(); }));
  server_initialized_.waitReady();
}

FakeUpstream::~FakeUpstream() { cleanUp(); };

void FakeUpstream::cleanUp() {
  if (thread_.get()) {
    dispatcher_->exit();
    thread_->join();
    thread_.reset();
  }
}

bool FakeUpstream::createNetworkFilterChain(Network::Connection& connection,
                                            const std::vector<Network::FilterFactoryCb>&) {
  Thread::LockGuard lock(lock_);
  connection.readDisable(true);
  auto connection_wrapper =
      std::make_unique<QueuedConnectionWrapper>(connection, allow_unexpected_disconnects_);
  connection_wrapper->moveIntoListBack(std::move(connection_wrapper), new_connections_);
  new_connection_event_.notifyOne();
  return true;
}

bool FakeUpstream::createListenerFilterChain(Network::ListenerFilterManager&) { return true; }

void FakeUpstream::threadRoutine() {
  handler_->addListener(listener_);

  server_initialized_.setReady();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  handler_.reset();
  {
    Thread::LockGuard lock(lock_);
    new_connections_.clear();
    consumed_connections_.clear();
  }
}

FakeHttpConnectionPtr FakeUpstream::waitForHttpConnection(Event::Dispatcher& client_dispatcher) {
  FakeHttpConnectionPtr connection;
  {
    Thread::LockGuard lock(lock_);
    while (new_connections_.empty()) {
      new_connection_event_.waitFor(lock_, std::chrono::milliseconds(5));
      if (new_connections_.empty()) {
        // Run the client dispatcher since we may need to process window updates, etc.
        client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
      }
    }

    ASSERT(!new_connections_.empty());
    connection =
        std::make_unique<FakeHttpConnection>(consumeConnection(), stats_store_, http_type_);
  }
  connection->initialize();
  connection->readDisable(false);
  return connection;
}

FakeHttpConnectionPtr
FakeUpstream::waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                                    std::vector<std::unique_ptr<FakeUpstream>>& upstreams) {
  for (;;) {
    for (auto it = upstreams.begin(); it != upstreams.end(); ++it) {
      FakeUpstream& upstream = **it;
      Thread::ReleasableLockGuard lock(upstream.lock_);
      if (upstream.new_connections_.empty()) {
        upstream.new_connection_event_.waitFor(upstream.lock_, std::chrono::milliseconds(5));
      }

      if (upstream.new_connections_.empty()) {
        // Run the client dispatcher since we may need to process window updates, etc.
        client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
      } else {
        FakeHttpConnectionPtr connection(new FakeHttpConnection(
            upstream.consumeConnection(), upstream.stats_store_, upstream.http_type_));
        lock.release();
        connection->initialize();
        connection->readDisable(false);
        return connection;
      }
    }
  }
}

FakeRawConnectionPtr FakeUpstream::waitForRawConnection(std::chrono::milliseconds wait_for_ms) {
  FakeRawConnectionPtr connection;
  {
    Thread::LockGuard lock(lock_);
    if (new_connections_.empty()) {
      ENVOY_LOG(debug, "waiting for raw connection");
      new_connection_event_.waitFor(lock_, wait_for_ms); // Safe since CondVar::wait won't throw.
    }

    if (new_connections_.empty()) {
      return nullptr;
    }
    connection = std::make_unique<FakeRawConnection>(consumeConnection());
  }
  connection->initialize();
  connection->readDisable(false);
  connection->enableHalfClose(enable_half_close_);
  return connection;
}

SharedConnectionWrapper& FakeUpstream::consumeConnection() {
  ASSERT(!new_connections_.empty());
  auto* const connection_wrapper = new_connections_.front().get();
  connection_wrapper->set_parented();
  connection_wrapper->moveBetweenLists(new_connections_, consumed_connections_);
  return connection_wrapper->shared_connection();
}

std::string FakeRawConnection::waitForData(uint64_t num_bytes) {
  Thread::LockGuard lock(lock_);
  while (data_.size() != num_bytes) {
    ENVOY_LOG(debug, "waiting for {} bytes of data", num_bytes);
    connection_event_.wait(lock_); // Safe since CondVar::wait won't throw.
  }
  return data_;
}

std::string
FakeRawConnection::waitForData(const std::function<bool(const std::string&)>& data_validator) {
  Thread::LockGuard lock(lock_);
  while (!data_validator(data_)) {
    ENVOY_LOG(debug, "waiting for data");
    connection_event_.wait(lock_); // Safe since CondVar::wait won't throw.
  }
  return data_;
}

void FakeRawConnection::write(const std::string& data, bool end_stream) {
  shared_connection_.executeOnDispatcher([&data, end_stream](Network::Connection& connection) {
    Buffer::OwnedImpl to_write(data);
    connection.write(to_write, end_stream);
  });
}

Network::FilterStatus FakeRawConnection::ReadFilter::onData(Buffer::Instance& data,
                                                            bool end_stream) {
  Thread::LockGuard lock(parent_.lock_);
  ENVOY_LOG(debug, "got {} bytes", data.length());
  parent_.data_.append(data.toString());
  parent_.half_closed_ = end_stream;
  data.drain(data.length());
  parent_.connection_event_.notifyOne();
  return Network::FilterStatus::StopIteration;
}
} // namespace Envoy
