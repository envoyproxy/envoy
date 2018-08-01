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

#include "absl/strings/str_cat.h"

using namespace std::chrono_literals;

using std::chrono::milliseconds;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;

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

AssertionResult FakeStream::waitForHeadersComplete(milliseconds timeout) {
  Thread::LockGuard lock(lock_);
  auto end_time = std::chrono::steady_clock::now() + timeout;
  while (!headers_) {
    if (std::chrono::steady_clock::now() >= end_time) {
      return AssertionFailure() << "Timed out waiting for headers.";
    }
    decoder_event_.waitFor(lock_, 5ms);
  }
  return AssertionSuccess();
}

AssertionResult FakeStream::waitForData(Event::Dispatcher& client_dispatcher, uint64_t body_length,
                                        milliseconds timeout) {
  Thread::LockGuard lock(lock_);
  auto start_time = std::chrono::steady_clock::now();
  while (bodyLength() < body_length) {
    if (std::chrono::steady_clock::now() >= start_time + timeout) {
      return AssertionFailure() << "Timed out waiting for data.";
    }
    decoder_event_.waitFor(lock_, 5ms);
    if (bodyLength() < body_length) {
      // Run the client dispatcher since we may need to process window updates, etc.
      client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
    }
  }
  return AssertionSuccess();
}

AssertionResult FakeStream::waitForEndStream(Event::Dispatcher& client_dispatcher,
                                             milliseconds timeout) {
  Thread::LockGuard lock(lock_);
  auto start_time = std::chrono::steady_clock::now();
  while (!end_stream_) {
    if (std::chrono::steady_clock::now() >= start_time + timeout) {
      return AssertionFailure() << "Timed out waiting for end of stream.";
    }
    decoder_event_.waitFor(lock_, 5ms);
    if (!end_stream_) {
      // Run the client dispatcher since we may need to process window updates, etc.
      client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
    }
  }
  return AssertionSuccess();
}

AssertionResult FakeStream::waitForReset(milliseconds timeout) {
  Thread::LockGuard lock(lock_);
  auto start_time = std::chrono::steady_clock::now();
  while (!saw_reset_) {
    if (std::chrono::steady_clock::now() >= start_time + timeout) {
      return AssertionFailure() << "Timed out waiting for reset.";
    }
    // Safe since CondVar::waitFor won't throw.
    decoder_event_.waitFor(lock_, 5ms);
  }
  return AssertionSuccess();
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

AssertionResult FakeConnectionBase::close(std::chrono::milliseconds timeout) {
  return shared_connection_.executeOnDispatcher(
      [](Network::Connection& connection) {
        connection.close(Network::ConnectionCloseType::FlushWrite);
      },
      timeout);
}

AssertionResult FakeConnectionBase::readDisable(bool disable, std::chrono::milliseconds timeout) {
  return shared_connection_.executeOnDispatcher(
      [disable](Network::Connection& connection) { connection.readDisable(disable); }, timeout);
}

AssertionResult FakeConnectionBase::enableHalfClose(bool enable,
                                                    std::chrono::milliseconds timeout) {
  return shared_connection_.executeOnDispatcher(
      [enable](Network::Connection& connection) { connection.enableHalfClose(enable); }, timeout);
}

Http::StreamDecoder& FakeHttpConnection::newStream(Http::StreamEncoder& encoder) {
  Thread::LockGuard lock(lock_);
  new_streams_.emplace_back(new FakeStream(*this, encoder));
  connection_event_.notifyOne();
  return *new_streams_.back();
}

AssertionResult FakeConnectionBase::waitForDisconnect(bool ignore_spurious_events,
                                                      milliseconds timeout) {
  ENVOY_LOG(trace, "FakeConnectionBase waiting for disconnect");
  auto end_time = std::chrono::steady_clock::now() + timeout;
  Thread::LockGuard lock(lock_);
  while (shared_connection_.connected()) {
    if (std::chrono::steady_clock::now() >= end_time) {
      return AssertionResult("Timed out waiting for disconnect.");
    }
    Thread::CondVar::WaitStatus status = connection_event_.waitFor(lock_, 5ms);
    // The default behavior of waitForDisconnect is to assume the test cleanly
    // calls waitForData, waitForNewStream, etc. to handle all events on the
    // connection. If the caller explicitly notes that other events should be
    // ignored, continue looping until a disconnect is detected. Otherwise fall
    // through and hit the assert below.
    if ((status == Thread::CondVar::WaitStatus::NoTimeout) && !ignore_spurious_events) {
      break;
    }
  }

  if (shared_connection_.connected()) {
    return AssertionFailure() << "Expected disconnect, but got a different event.";
  }
  ENVOY_LOG(trace, "FakeConnectionBase done waiting for disconnect");
  return AssertionSuccess();
}

AssertionResult FakeConnectionBase::waitForHalfClose(bool ignore_spurious_events,
                                                     milliseconds timeout) {
  auto end_time = std::chrono::steady_clock::now() + timeout;
  Thread::LockGuard lock(lock_);
  while (!half_closed_) {
    if (std::chrono::steady_clock::now() >= end_time) {
      return AssertionFailure() << "Timed out waiting for half close.";
    }
    connection_event_.waitFor(lock_, 5ms); // Safe since CondVar::waitFor won't throw.
    // The default behavior of waitForHalfClose is to assume the test cleanly
    // calls waitForData, waitForNewStream, etc. to handle all events on the
    // connection. If the caller explicitly notes that other events should be
    // ignored, continue looping until a disconnect is detected. Otherwise fall
    // through and hit the assert below.
    if (!ignore_spurious_events) {
      break;
    }
  }

  return half_closed_
             ? AssertionSuccess()
             : (AssertionFailure() << "Expected half close event, but got a different event.");
}

AssertionResult FakeHttpConnection::waitForNewStream(Event::Dispatcher& client_dispatcher,
                                                     FakeStreamPtr& stream,
                                                     bool ignore_spurious_events,
                                                     milliseconds timeout) {
  auto end_time = std::chrono::steady_clock::now() + timeout;
  Thread::LockGuard lock(lock_);
  while (new_streams_.empty()) {
    if (std::chrono::steady_clock::now() >= end_time) {
      return AssertionResult("Timed out waiting for new stream.");
    }
    Thread::CondVar::WaitStatus status = connection_event_.waitFor(lock_, 5ms);
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

  if (new_streams_.empty()) {
    return AssertionFailure() << "Expected new stream event, but got a different event.";
  }
  stream = std::move(new_streams_.front());
  new_streams_.pop_front();
  return AssertionSuccess();
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
    : http_type_(type), socket_(std::move(listen_socket)), api_(new Api::Impl(milliseconds(10000))),
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

AssertionResult FakeUpstream::waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                                                    FakeHttpConnectionPtr& connection,
                                                    milliseconds timeout) {
  auto end_time = std::chrono::steady_clock::now() + timeout;
  {
    Thread::LockGuard lock(lock_);
    while (new_connections_.empty()) {
      if (std::chrono::steady_clock::now() >= end_time) {
        return AssertionFailure() << "Timed out waiting for new connection.";
      }
      new_connection_event_.waitFor(lock_, 5ms);
      if (new_connections_.empty()) {
        // Run the client dispatcher since we may need to process window updates, etc.
        client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
      }
    }

    if (new_connections_.empty()) {
      return AssertionFailure() << "Got a new connection event, but didn't create a connection.";
    }
    connection =
        std::make_unique<FakeHttpConnection>(consumeConnection(), stats_store_, http_type_);
  }
  VERIFY_ASSERTION(connection->initialize());
  VERIFY_ASSERTION(connection->readDisable(false));
  return AssertionSuccess();
}

AssertionResult
FakeUpstream::waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                                    std::vector<std::unique_ptr<FakeUpstream>>& upstreams,
                                    FakeHttpConnectionPtr& connection, milliseconds timeout) {
  auto end_time = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < end_time) {
    for (auto it = upstreams.begin(); it != upstreams.end(); ++it) {
      FakeUpstream& upstream = **it;
      Thread::ReleasableLockGuard lock(upstream.lock_);
      if (upstream.new_connections_.empty()) {
        upstream.new_connection_event_.waitFor(upstream.lock_, 5ms);
      }

      if (upstream.new_connections_.empty()) {
        // Run the client dispatcher since we may need to process window updates, etc.
        client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
      } else {
        connection = std::make_unique<FakeHttpConnection>(
            upstream.consumeConnection(), upstream.stats_store_, upstream.http_type_);
        lock.release();
        VERIFY_ASSERTION(connection->initialize());
        VERIFY_ASSERTION(connection->readDisable(false));
        return AssertionSuccess();
      }
    }
  }
  return AssertionFailure() << "Timed out waiting for HTTP connection.";
}

AssertionResult FakeUpstream::waitForRawConnection(FakeRawConnectionPtr& connection,
                                                   milliseconds timeout) {
  {
    Thread::LockGuard lock(lock_);
    if (new_connections_.empty()) {
      ENVOY_LOG(debug, "waiting for raw connection");
      new_connection_event_.waitFor(lock_, timeout); // Safe since CondVar::waitFor won't throw.
    }

    if (new_connections_.empty()) {
      return AssertionFailure() << "Timed out waiting for raw connection";
    }
    connection = std::make_unique<FakeRawConnection>(consumeConnection());
  }
  VERIFY_ASSERTION(connection->initialize());
  VERIFY_ASSERTION(connection->readDisable(false));
  VERIFY_ASSERTION(connection->enableHalfClose(enable_half_close_));
  return AssertionSuccess();
}

SharedConnectionWrapper& FakeUpstream::consumeConnection() {
  ASSERT(!new_connections_.empty());
  auto* const connection_wrapper = new_connections_.front().get();
  connection_wrapper->set_parented();
  connection_wrapper->moveBetweenLists(new_connections_, consumed_connections_);
  return connection_wrapper->shared_connection();
}

AssertionResult FakeRawConnection::waitForData(uint64_t num_bytes, std::string* data,
                                               milliseconds timeout) {
  Thread::LockGuard lock(lock_);
  ENVOY_LOG(debug, "waiting for {} bytes of data", num_bytes);
  auto end_time = std::chrono::steady_clock::now() + timeout;
  while (data_.size() != num_bytes) {
    if (std::chrono::steady_clock::now() >= end_time) {
      return AssertionFailure() << "Timed out waiting for data.";
    }
    connection_event_.waitFor(lock_, 5ms); // Safe since CondVar::waitFor won't throw.
  }
  if (data != nullptr) {
    *data = data_;
  }
  return AssertionSuccess();
}

AssertionResult
FakeRawConnection::waitForData(const std::function<bool(const std::string&)>& data_validator,
                               std::string* data, milliseconds timeout) {
  Thread::LockGuard lock(lock_);
  ENVOY_LOG(debug, "waiting for data");
  auto end_time = std::chrono::steady_clock::now() + timeout;
  while (!data_validator(data_)) {
    if (std::chrono::steady_clock::now() >= end_time) {
      return AssertionFailure() << "Timed out waiting for data.";
    }
    connection_event_.waitFor(lock_, 5ms); // Safe since CondVar::waitFor won't throw.
  }
  if (data != nullptr) {
    *data = data_;
  }
  return AssertionSuccess();
}

AssertionResult FakeRawConnection::write(const std::string& data, bool end_stream,
                                         milliseconds timeout) {
  return shared_connection_.executeOnDispatcher(
      [&data, end_stream](Network::Connection& connection) {
        Buffer::OwnedImpl to_write(data);
        connection.write(to_write, end_stream);
      },
      timeout);
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
