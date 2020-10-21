#include "test/integration/fake_upstream.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http1/codec_impl_legacy.h"
#include "common/http/http2/codec_impl_legacy.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/utility.h"

#include "server/connection_handler_impl.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"

using namespace std::chrono_literals;

using std::chrono::milliseconds;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;

namespace Envoy {
FakeStream::FakeStream(FakeHttpConnection& parent, Http::ResponseEncoder& encoder,
                       Event::TestTimeSystem& time_system)
    : parent_(parent), encoder_(encoder), time_system_(time_system) {
  encoder.getStream().addCallbacks(*this);
}

void FakeStream::decodeHeaders(Http::RequestHeaderMapPtr&& headers, bool end_stream) {
  absl::MutexLock lock(&lock_);
  headers_ = std::move(headers);
  setEndStream(end_stream);
}

void FakeStream::decodeData(Buffer::Instance& data, bool end_stream) {
  received_data_ = true;
  absl::MutexLock lock(&lock_);
  body_.add(data);
  setEndStream(end_stream);
}

void FakeStream::decodeTrailers(Http::RequestTrailerMapPtr&& trailers) {
  absl::MutexLock lock(&lock_);
  setEndStream(true);
  trailers_ = std::move(trailers);
}

void FakeStream::decodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr) {
  for (const auto& metadata : *metadata_map_ptr) {
    duplicated_metadata_key_count_[metadata.first]++;
    metadata_map_.insert(metadata);
  }
}

void FakeStream::postToConnectionThread(std::function<void()> cb) {
  parent_.connection().dispatcher().post(cb);
}

void FakeStream::encode100ContinueHeaders(const Http::ResponseHeaderMap& headers) {
  std::shared_ptr<Http::ResponseHeaderMap> headers_copy(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers));
  parent_.connection().dispatcher().post(
      [this, headers_copy]() -> void { encoder_.encode100ContinueHeaders(*headers_copy); });
}

void FakeStream::encodeHeaders(const Http::HeaderMap& headers, bool end_stream) {
  std::shared_ptr<Http::ResponseHeaderMap> headers_copy(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers));
  if (add_served_by_header_) {
    headers_copy->addCopy(Http::LowerCaseString("x-served-by"),
                          parent_.connection().localAddress()->asString());
  }

  parent_.connection().dispatcher().post([this, headers_copy, end_stream]() -> void {
    encoder_.encodeHeaders(*headers_copy, end_stream);
  });
}

void FakeStream::encodeData(absl::string_view data, bool end_stream) {
  parent_.connection().dispatcher().post([this, data, end_stream]() -> void {
    Buffer::OwnedImpl fake_data(data.data(), data.size());
    encoder_.encodeData(fake_data, end_stream);
  });
}

void FakeStream::encodeData(uint64_t size, bool end_stream) {
  parent_.connection().dispatcher().post([this, size, end_stream]() -> void {
    Buffer::OwnedImpl data(std::string(size, 'a'));
    encoder_.encodeData(data, end_stream);
  });
}

void FakeStream::encodeData(Buffer::Instance& data, bool end_stream) {
  std::shared_ptr<Buffer::Instance> data_copy = std::make_shared<Buffer::OwnedImpl>(data);
  parent_.connection().dispatcher().post(
      [this, data_copy, end_stream]() -> void { encoder_.encodeData(*data_copy, end_stream); });
}

void FakeStream::encodeTrailers(const Http::HeaderMap& trailers) {
  std::shared_ptr<Http::ResponseTrailerMap> trailers_copy(
      Http::createHeaderMap<Http::ResponseTrailerMapImpl>(trailers));
  parent_.connection().dispatcher().post(
      [this, trailers_copy]() -> void { encoder_.encodeTrailers(*trailers_copy); });
}

void FakeStream::encodeResetStream() {
  parent_.connection().dispatcher().post(
      [this]() -> void { encoder_.getStream().resetStream(Http::StreamResetReason::LocalReset); });
}

void FakeStream::encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) {
  parent_.connection().dispatcher().post(
      [this, &metadata_map_vector]() -> void { encoder_.encodeMetadata(metadata_map_vector); });
}

void FakeStream::readDisable(bool disable) {
  parent_.connection().dispatcher().post(
      [this, disable]() -> void { encoder_.getStream().readDisable(disable); });
}

void FakeStream::onResetStream(Http::StreamResetReason, absl::string_view) {
  absl::MutexLock lock(&lock_);
  saw_reset_ = true;
}

AssertionResult FakeStream::waitForHeadersComplete(milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  const auto reached = [this]()
                           ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return headers_ != nullptr; };
  if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
    return AssertionFailure() << "Timed out waiting for headers.";
  }
  return AssertionSuccess();
}

namespace {
// Perform a wait on a condition while still allowing for periodic client dispatcher runs that
// occur on the current thread.
bool waitForWithDispatcherRun(Event::TestTimeSystem& time_system, absl::Mutex& lock,
                              const std::function<bool()>& condition,
                              Event::Dispatcher& client_dispatcher, milliseconds timeout)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (bound.withinBound()) {
    // Wake up every 5ms to run the client dispatcher.
    if (time_system.waitFor(lock, absl::Condition(&condition), 5ms)) {
      return true;
    }

    // Run the client dispatcher since we may need to process window updates, etc.
    client_dispatcher.run(Event::Dispatcher::RunType::NonBlock);
  }
  return false;
}
} // namespace

AssertionResult FakeStream::waitForData(Event::Dispatcher& client_dispatcher, uint64_t body_length,
                                        milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!waitForWithDispatcherRun(
          time_system_, lock_,
          [this, body_length]()
              ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return (body_.length() >= body_length); },
          client_dispatcher, timeout)) {
    return AssertionFailure() << "Timed out waiting for data.";
  }
  return AssertionSuccess();
}

AssertionResult FakeStream::waitForData(Event::Dispatcher& client_dispatcher,
                                        absl::string_view data, milliseconds timeout) {
  auto succeeded = waitForData(client_dispatcher, data.length(), timeout);
  if (succeeded) {
    Buffer::OwnedImpl buffer(data.data(), data.length());
    if (!TestUtility::buffersEqual(body(), buffer)) {
      return AssertionFailure() << body().toString() << " not equal to " << data;
    }
  }
  return succeeded;
}

AssertionResult FakeStream::waitForEndStream(Event::Dispatcher& client_dispatcher,
                                             milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!waitForWithDispatcherRun(
          time_system_, lock_,
          [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return end_stream_; }, client_dispatcher,
          timeout)) {
    return AssertionFailure() << "Timed out waiting for end of stream.";
  }
  return AssertionSuccess();
}

AssertionResult FakeStream::waitForReset(milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!time_system_.waitFor(lock_, absl::Condition(&saw_reset_), timeout)) {
    return AssertionFailure() << "Timed out waiting for reset.";
  }
  return AssertionSuccess();
}

void FakeStream::startGrpcStream() {
  encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
}

void FakeStream::finishGrpcStream(Grpc::Status::GrpcStatus status) {
  encodeTrailers(Http::TestResponseTrailerMapImpl{
      {"grpc-status", std::to_string(static_cast<uint32_t>(status))}});
}

// The TestHttp1ServerConnectionImpl outlives its underlying Network::Connection
// so must not access the Connection on teardown. To achieve this, clear the
// read disable calls to avoid checking / editing the Connection blocked state.
class TestHttp1ServerConnectionImpl : public Http::Http1::ServerConnectionImpl {
public:
  using Http::Http1::ServerConnectionImpl::ServerConnectionImpl;

  void onMessageComplete() override {
    ServerConnectionImpl::onMessageComplete();

    if (activeRequest().has_value() && activeRequest().value().request_decoder_) {
      // Undo the read disable from the base class - we have many tests which
      // waitForDisconnect after a full request has been read which will not
      // receive the disconnect if reading is disabled.
      activeRequest().value().response_encoder_.readDisable(false);
    }
  }
  ~TestHttp1ServerConnectionImpl() override {
    if (activeRequest().has_value()) {
      activeRequest().value().response_encoder_.clearReadDisableCallsForTests();
    }
  }
};

namespace Legacy {
class TestHttp1ServerConnectionImpl : public Http::Legacy::Http1::ServerConnectionImpl {
public:
  using Http::Legacy::Http1::ServerConnectionImpl::ServerConnectionImpl;

  void onMessageComplete() override {
    ServerConnectionImpl::onMessageComplete();

    if (activeRequest().has_value() && activeRequest().value().request_decoder_) {
      // Undo the read disable from the base class - we have many tests which
      // waitForDisconnect after a full request has been read which will not
      // receive the disconnect if reading is disabled.
      activeRequest().value().response_encoder_.readDisable(false);
    }
  }
  ~TestHttp1ServerConnectionImpl() override {
    if (activeRequest().has_value()) {
      activeRequest().value().response_encoder_.clearReadDisableCallsForTests();
    }
  }
};
} // namespace Legacy

FakeHttpConnection::FakeHttpConnection(
    FakeUpstream& fake_upstream, SharedConnectionWrapper& shared_connection, Type type,
    Event::TestTimeSystem& time_system, uint32_t max_request_headers_kb,
    uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action)
    : FakeConnectionBase(shared_connection, time_system), type_(type) {
  if (type == Type::HTTP1) {
    Http::Http1Settings http1_settings;
    // For the purpose of testing, we always have the upstream encode the trailers if any
    http1_settings.enable_trailers_ = true;
    Http::Http1::CodecStats& stats = fake_upstream.http1CodecStats();
#ifdef ENVOY_USE_NEW_CODECS_IN_INTEGRATION_TESTS
    codec_ = std::make_unique<TestHttp1ServerConnectionImpl>(
        shared_connection_.connection(), stats, *this, http1_settings, max_request_headers_kb,
        max_request_headers_count, headers_with_underscores_action);
#else
    codec_ = std::make_unique<Legacy::TestHttp1ServerConnectionImpl>(
        shared_connection_.connection(), stats, *this, http1_settings, max_request_headers_kb,
        max_request_headers_count, headers_with_underscores_action);
#endif
  } else {
    envoy::config::core::v3::Http2ProtocolOptions http2_options =
        ::Envoy::Http2::Utility::initializeAndValidateOptions(
            envoy::config::core::v3::Http2ProtocolOptions());
    http2_options.set_allow_connect(true);
    http2_options.set_allow_metadata(true);
    Http::Http2::CodecStats& stats = fake_upstream.http2CodecStats();
#ifdef ENVOY_USE_NEW_CODECS_IN_INTEGRATION_TESTS
    codec_ = std::make_unique<Http::Http2::ServerConnectionImpl>(
        shared_connection_.connection(), *this, stats, random_, http2_options,
        max_request_headers_kb, max_request_headers_count, headers_with_underscores_action);
#else
    codec_ = std::make_unique<Http::Legacy::Http2::ServerConnectionImpl>(
        shared_connection_.connection(), *this, stats, random_, http2_options,
        max_request_headers_kb, max_request_headers_count, headers_with_underscores_action);
#endif
    ASSERT(type == Type::HTTP2);
  }
  shared_connection_.connection().addReadFilter(
      Network::ReadFilterSharedPtr{new ReadFilter(*this)});
}

AssertionResult FakeConnectionBase::close(std::chrono::milliseconds timeout) {
  ENVOY_LOG(trace, "FakeConnectionBase close");
  if (!shared_connection_.connected()) {
    return AssertionSuccess();
  }
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

Http::RequestDecoder& FakeHttpConnection::newStream(Http::ResponseEncoder& encoder, bool) {
  absl::MutexLock lock(&lock_);
  new_streams_.emplace_back(new FakeStream(*this, encoder, time_system_));
  return *new_streams_.back();
}

void FakeHttpConnection::onGoAway(Http::GoAwayErrorCode code) {
  ASSERT(type_ == Type::HTTP2);
  // Usually indicates connection level errors, no operations are needed since
  // the connection will be closed soon.
  ENVOY_LOG(info, "FakeHttpConnection receives GOAWAY: ", code);
}

AssertionResult FakeConnectionBase::waitForDisconnect(milliseconds timeout) {
  ENVOY_LOG(trace, "FakeConnectionBase waiting for disconnect");
  absl::MutexLock lock(&lock_);
  const auto reached = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    return !shared_connection_.connectedLockHeld();
  };

  if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
    return AssertionFailure() << "Timed out waiting for disconnect.";
  }
  ENVOY_LOG(trace, "FakeConnectionBase done waiting for disconnect");
  return AssertionSuccess();
}

AssertionResult FakeConnectionBase::waitForHalfClose(milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!time_system_.waitFor(lock_, absl::Condition(&half_closed_), timeout)) {
    return AssertionFailure() << "Timed out waiting for half close.";
  }
  return AssertionSuccess();
}

AssertionResult FakeHttpConnection::waitForNewStream(Event::Dispatcher& client_dispatcher,
                                                     FakeStreamPtr& stream,
                                                     std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!waitForWithDispatcherRun(
          time_system_, lock_,
          [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return !new_streams_.empty(); },
          client_dispatcher, timeout)) {
    return AssertionFailure() << "Timed out waiting for new stream.";
  }
  stream = std::move(new_streams_.front());
  new_streams_.pop_front();
  return AssertionSuccess();
}

FakeUpstream::FakeUpstream(const std::string& uds_path, FakeHttpConnection::Type type,
                           Event::TestTimeSystem& time_system)
    : FakeUpstream(Network::Test::createRawBufferSocketFactory(),
                   Network::SocketPtr{new Network::UdsListenSocket(
                       std::make_shared<Network::Address::PipeInstance>(uds_path))},
                   type, time_system, false) {
  ENVOY_LOG(info, "starting fake server on unix domain socket {}", uds_path);
}

static Network::SocketPtr
makeTcpListenSocket(const Network::Address::InstanceConstSharedPtr& address) {
  return std::make_unique<Network::TcpListenSocket>(address, nullptr, true);
}

static Network::SocketPtr makeTcpListenSocket(uint32_t port, Network::Address::IpVersion version) {
  return makeTcpListenSocket(Network::Utility::parseInternetAddress(
      Network::Test::getLoopbackAddressString(version), port));
}

static Network::SocketPtr
makeUdpListenSocket(const Network::Address::InstanceConstSharedPtr& address) {
  auto socket = std::make_unique<Network::UdpListenSocket>(address, nullptr, true);
  // TODO(mattklein123): These options are set in multiple locations. We should centralize them for
  // UDP listeners.
  socket->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
  socket->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
  return socket;
}

FakeUpstream::FakeUpstream(const Network::Address::InstanceConstSharedPtr& address,
                           FakeHttpConnection::Type type, Event::TestTimeSystem& time_system,
                           bool enable_half_close, bool udp_fake_upstream)
    : FakeUpstream(Network::Test::createRawBufferSocketFactory(),
                   udp_fake_upstream ? makeUdpListenSocket(address) : makeTcpListenSocket(address),
                   type, time_system, enable_half_close) {
  ENVOY_LOG(info, "starting fake server on socket {}:{}. Address version is {}. UDP={}",
            address->ip()->addressAsString(), address->ip()->port(),
            Network::Test::addressVersionAsString(address->ip()->version()), udp_fake_upstream);
}

FakeUpstream::FakeUpstream(uint32_t port, FakeHttpConnection::Type type,
                           Network::Address::IpVersion version, Event::TestTimeSystem& time_system,
                           bool enable_half_close)
    : FakeUpstream(Network::Test::createRawBufferSocketFactory(),
                   makeTcpListenSocket(port, version), type, time_system, enable_half_close) {
  ENVOY_LOG(info, "starting fake server on port {}. Address version is {}",
            localAddress()->ip()->port(), Network::Test::addressVersionAsString(version));
}

FakeUpstream::FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                           uint32_t port, FakeHttpConnection::Type type,
                           Network::Address::IpVersion version, Event::TestTimeSystem& time_system)
    : FakeUpstream(std::move(transport_socket_factory), makeTcpListenSocket(port, version), type,
                   time_system, false) {
  ENVOY_LOG(info, "starting fake SSL server on port {}. Address version is {}",
            localAddress()->ip()->port(), Network::Test::addressVersionAsString(version));
}

FakeUpstream::FakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                           Network::SocketPtr&& listen_socket, FakeHttpConnection::Type type,
                           Event::TestTimeSystem& time_system, bool enable_half_close)
    : http_type_(type), socket_(Network::SocketSharedPtr(listen_socket.release())),
      socket_factory_(std::make_shared<FakeListenSocketFactory>(socket_)),
      api_(Api::createApiForTest(stats_store_)), time_system_(time_system),
      dispatcher_(api_->allocateDispatcher("fake_upstream")),
      handler_(new Server::ConnectionHandlerImpl(*dispatcher_, 0)),
      read_disable_on_new_connection_(true), enable_half_close_(enable_half_close),
      listener_(*this),
      filter_chain_(Network::Test::createEmptyFilterChain(std::move(transport_socket_factory))) {
  thread_ = api_->threadFactory().createThread([this]() -> void { threadRoutine(); });
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
  absl::MutexLock lock(&lock_);
  if (read_disable_on_new_connection_) {
    connection.readDisable(true);
  }
  auto connection_wrapper = std::make_unique<SharedConnectionWrapper>(connection);
  LinkedList::moveIntoListBack(std::move(connection_wrapper), new_connections_);
  return true;
}

bool FakeUpstream::createListenerFilterChain(Network::ListenerFilterManager&) { return true; }

void FakeUpstream::createUdpListenerFilterChain(Network::UdpListenerFilterManager& udp_listener,
                                                Network::UdpReadFilterCallbacks& callbacks) {
  udp_listener.addReadFilter(std::make_unique<FakeUpstreamUdpFilter>(*this, callbacks));
}

void FakeUpstream::threadRoutine() {
  handler_->addListener(absl::nullopt, listener_);
  server_initialized_.setReady();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  handler_.reset();
  {
    absl::MutexLock lock(&lock_);
    new_connections_.clear();
    consumed_connections_.clear();
  }
}

AssertionResult FakeUpstream::waitForHttpConnection(
    Event::Dispatcher& client_dispatcher, FakeHttpConnectionPtr& connection, milliseconds timeout,
    uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action) {
  {
    absl::MutexLock lock(&lock_);
    if (!waitForWithDispatcherRun(
            time_system_, lock_,
            [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return !new_connections_.empty(); },
            client_dispatcher, timeout)) {
      return AssertionFailure() << "Timed out waiting for new connection.";
    }

    connection = std::make_unique<FakeHttpConnection>(
        *this, consumeConnection(), http_type_, time_system_, max_request_headers_kb,
        max_request_headers_count, headers_with_underscores_action);
  }
  VERIFY_ASSERTION(connection->initialize());
  if (read_disable_on_new_connection_) {
    VERIFY_ASSERTION(connection->readDisable(false));
  }
  return AssertionSuccess();
}

AssertionResult
FakeUpstream::waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                                    std::vector<std::unique_ptr<FakeUpstream>>& upstreams,
                                    FakeHttpConnectionPtr& connection, milliseconds timeout) {
  if (upstreams.empty()) {
    return AssertionFailure() << "No upstreams configured.";
  }
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (bound.withinBound()) {
    for (auto& it : upstreams) {
      FakeUpstream& upstream = *it;
      {
        absl::MutexLock lock(&upstream.lock_);
        if (!waitForWithDispatcherRun(
                upstream.time_system_, upstream.lock_,
                [&upstream]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(upstream.lock_) {
                  return !upstream.new_connections_.empty();
                },
                client_dispatcher, 5ms)) {
          continue;
        }
        connection = std::make_unique<FakeHttpConnection>(
            upstream, upstream.consumeConnection(), upstream.http_type_, upstream.timeSystem(),
            Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
            envoy::config::core::v3::HttpProtocolOptions::ALLOW);
      }
      VERIFY_ASSERTION(connection->initialize());
      VERIFY_ASSERTION(connection->readDisable(false));
      return AssertionSuccess();
    }
  }
  return AssertionFailure() << "Timed out waiting for HTTP connection.";
}

AssertionResult FakeUpstream::waitForRawConnection(FakeRawConnectionPtr& connection,
                                                   milliseconds timeout) {
  {
    absl::MutexLock lock(&lock_);
    const auto reached = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
      return !new_connections_.empty();
    };

    ENVOY_LOG(debug, "waiting for raw connection");
    if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
      return AssertionFailure() << "Timed out waiting for raw connection";
    }
    connection = std::make_unique<FakeRawConnection>(consumeConnection(), timeSystem());
  }
  VERIFY_ASSERTION(connection->initialize());
  VERIFY_ASSERTION(connection->readDisable(false));
  VERIFY_ASSERTION(connection->enableHalfClose(enable_half_close_));
  return AssertionSuccess();
}

SharedConnectionWrapper& FakeUpstream::consumeConnection() {
  ASSERT(!new_connections_.empty());
  auto* const connection_wrapper = new_connections_.front().get();
  connection_wrapper->setParented();
  connection_wrapper->moveBetweenLists(new_connections_, consumed_connections_);
  return *connection_wrapper;
}

testing::AssertionResult FakeUpstream::waitForUdpDatagram(Network::UdpRecvData& data_to_fill,
                                                          std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  const auto reached = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    return !received_datagrams_.empty();
  };

  if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
    return AssertionFailure() << "Timed out waiting for UDP datagram.";
  }

  data_to_fill = std::move(received_datagrams_.front());
  received_datagrams_.pop_front();
  return AssertionSuccess();
}

void FakeUpstream::onRecvDatagram(Network::UdpRecvData& data) {
  absl::MutexLock lock(&lock_);
  received_datagrams_.emplace_back(std::move(data));
}

void FakeUpstream::sendUdpDatagram(const std::string& buffer,
                                   const Network::Address::InstanceConstSharedPtr& peer) {
  dispatcher_->post([this, buffer, peer] {
    const auto rc = Network::Utility::writeToSocket(socket_->ioHandle(), Buffer::OwnedImpl(buffer),
                                                    nullptr, *peer);
    EXPECT_TRUE(rc.rc_ == buffer.length());
  });
}

testing::AssertionResult FakeUpstream::rawWriteConnection(uint32_t index, const std::string& data,
                                                          bool end_stream,
                                                          std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  auto iter = consumed_connections_.begin();
  std::advance(iter, index);
  return (*iter)->executeOnDispatcher(
      [data, end_stream](Network::Connection& connection) {
        ASSERT(connection.state() == Network::Connection::State::Open);
        Buffer::OwnedImpl buffer(data);
        connection.write(buffer, end_stream);
      },
      timeout);
}

AssertionResult FakeRawConnection::waitForData(uint64_t num_bytes, std::string* data,
                                               milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  const auto reached = [this, num_bytes]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    return data_.size() == num_bytes;
  };
  ENVOY_LOG(debug, "waiting for {} bytes of data", num_bytes);
  if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
    return AssertionFailure() << fmt::format(
               "Timed out waiting for data. Got '{}', waiting for {} bytes.", data_, num_bytes);
  }
  if (data != nullptr) {
    *data = data_;
  }
  return AssertionSuccess();
}

AssertionResult
FakeRawConnection::waitForData(const std::function<bool(const std::string&)>& data_validator,
                               std::string* data, milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  const auto reached = [this, &data_validator]()
                           ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return data_validator(data_); };
  ENVOY_LOG(debug, "waiting for data");
  if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
    return AssertionFailure() << "Timed out waiting for data.";
  }
  if (data != nullptr) {
    *data = data_;
  }
  return AssertionSuccess();
}

AssertionResult FakeRawConnection::write(const std::string& data, bool end_stream,
                                         milliseconds timeout) {
  return shared_connection_.executeOnDispatcher(
      [data, end_stream](Network::Connection& connection) {
        Buffer::OwnedImpl to_write(data);
        connection.write(to_write, end_stream);
      },
      timeout);
}

Network::FilterStatus FakeRawConnection::ReadFilter::onData(Buffer::Instance& data,
                                                            bool end_stream) {
  absl::MutexLock lock(&parent_.lock_);
  ENVOY_LOG(debug, "got {} bytes, end_stream {}", data.length(), end_stream);
  parent_.data_.append(data.toString());
  parent_.half_closed_ = end_stream;
  data.drain(data.length());
  return Network::FilterStatus::StopIteration;
}
} // namespace Envoy
