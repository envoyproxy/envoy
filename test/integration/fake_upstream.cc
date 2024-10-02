#include "test/integration/fake_upstream.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/server_codec_impl.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#endif

#include "source/common/listener_manager/connection_handler_impl.h"

#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"

using namespace std::chrono_literals;

using std::chrono::milliseconds;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;

namespace Envoy {

FakeStream::FakeStream(FakeHttpConnection& parent, Http::ResponseEncoder& encoder,
                       Event::TestTimeSystem& time_system)
    : parent_(parent), encoder_(encoder), time_system_(time_system),
      header_validator_(parent.makeHeaderValidator()) {
  encoder.getStream().addCallbacks(*this);
}

void FakeStream::decodeHeaders(Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) {
  absl::MutexLock lock(&lock_);
  headers_ = std::move(headers);
  if (header_validator_) {
    header_validator_->transformRequestHeaders(*headers_);
  }
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
  parent_.postToConnectionThread(cb);
}

void FakeStream::encode1xxHeaders(const Http::ResponseHeaderMap& headers) {
  std::shared_ptr<Http::ResponseHeaderMap> headers_copy(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers));
  postToConnectionThread([this, headers_copy]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    encoder_.encode1xxHeaders(*headers_copy);
  });
}

void FakeStream::encodeHeaders(const Http::HeaderMap& headers, bool end_stream) {
  std::shared_ptr<Http::ResponseHeaderMap> headers_copy(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers));
  if (add_served_by_header_) {
    headers_copy->addCopy(Http::LowerCaseString("x-served-by"),
                          parent_.connection().connectionInfoProvider().localAddress()->asString());
  }

  if (header_validator_) {
    // Ignore validation results
    auto result = header_validator_->transformResponseHeaders(*headers_copy);
    if (result.new_headers) {
      headers_copy = std::move(result.new_headers);
    }
  }

  postToConnectionThread([this, headers_copy = std::move(headers_copy), end_stream]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    encoder_.encodeHeaders(*headers_copy, end_stream);
  });
}

void FakeStream::encodeData(std::string data, bool end_stream) {
  postToConnectionThread([this, data, end_stream]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    Buffer::OwnedImpl fake_data(data.data(), data.size());
    encoder_.encodeData(fake_data, end_stream);
  });
}

void FakeStream::encodeData(uint64_t size, bool end_stream) {
  postToConnectionThread([this, size, end_stream]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    Buffer::OwnedImpl data(std::string(size, 'a'));
    encoder_.encodeData(data, end_stream);
  });
}

void FakeStream::encodeData(Buffer::Instance& data, bool end_stream) {
  std::shared_ptr<Buffer::Instance> data_copy = std::make_shared<Buffer::OwnedImpl>(data);
  postToConnectionThread([this, data_copy, end_stream]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    encoder_.encodeData(*data_copy, end_stream);
  });
}

void FakeStream::encodeTrailers(const Http::HeaderMap& trailers) {
  std::shared_ptr<Http::ResponseTrailerMap> trailers_copy(
      Http::createHeaderMap<Http::ResponseTrailerMapImpl>(trailers));
  postToConnectionThread([this, trailers_copy]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    encoder_.encodeTrailers(*trailers_copy);
  });
}

void FakeStream::encodeResetStream() {
  postToConnectionThread([this]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    if (parent_.type() == Http::CodecType::HTTP1) {
      parent_.connection().close(Network::ConnectionCloseType::FlushWrite);
    } else {
      encoder_.getStream().resetStream(Http::StreamResetReason::LocalReset);
    }
  });
}

void FakeStream::encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) {
  postToConnectionThread([this, &metadata_map_vector]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    encoder_.encodeMetadata(metadata_map_vector);
  });
}

void FakeStream::readDisable(bool disable) {
  postToConnectionThread([this, disable]() -> void {
    {
      absl::MutexLock lock(&lock_);
      if (!parent_.connected() || saw_reset_) {
        // Encoded already deleted.
        return;
      }
    }
    encoder_.getStream().readDisable(disable);
  });
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
    // Wake up periodically to run the client dispatcher.
    if (time_system.waitFor(lock, absl::Condition(&condition), 5ms * TIMEOUT_FACTOR)) {
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

AssertionResult FakeStream::waitForData(Event::Dispatcher& client_dispatcher,
                                        const FakeStream::ValidatorFunction& data_validator,
                                        std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!waitForWithDispatcherRun(
          time_system_, lock_,
          [this, data_validator]()
              ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return data_validator(body_.toString()); },
          client_dispatcher, timeout)) {
    return AssertionFailure() << "Timed out waiting for data.";
  }
  return AssertionSuccess();
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

void FakeStream::startGrpcStream(bool send_headers) {
  ASSERT(!grpc_stream_started_, "gRPC stream should not be started more than once");
  grpc_stream_started_ = true;
  if (send_headers) {
    encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  }
}

void FakeStream::finishGrpcStream(Grpc::Status::GrpcStatus status) {
  encodeTrailers(Http::TestResponseTrailerMapImpl{
      {"grpc-status", std::to_string(static_cast<uint32_t>(status))}});
}

class TestHttp1ServerConnectionImpl : public Http::Http1::ServerConnectionImpl {
public:
  using Http::Http1::ServerConnectionImpl::ServerConnectionImpl;
};

class TestHttp2ServerConnectionImpl : public Http::Http2::ServerConnectionImpl {
public:
  TestHttp2ServerConnectionImpl(
      Network::Connection& connection, Http::ServerConnectionCallbacks& callbacks,
      Http::Http2::CodecStats& stats, Random::RandomGenerator& random_generator,
      const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
      const uint32_t max_request_headers_kb, const uint32_t max_request_headers_count,
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
          headers_with_underscores_action,
      Server::OverloadManager& overload_manager)
      : ServerConnectionImpl(connection, callbacks, stats, random_generator, http2_options,
                             max_request_headers_kb, max_request_headers_count,
                             headers_with_underscores_action, overload_manager) {}

  void updateConcurrentStreams(uint32_t max_streams) {
    absl::InlinedVector<http2::adapter::Http2Setting, 1> settings;
    settings.push_back({http2::adapter::MAX_CONCURRENT_STREAMS, max_streams});
    adapter_->SubmitSettings(settings);
    const int rc = adapter_->Send();
    ASSERT(rc == 0);
  }
};

namespace {
// Fake upstream codec will not do path normalization, so the tests can observe
// the path forwarded by Envoy.
::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
fakeUpstreamHeaderValidatorConfig() {
  ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig config;
  config.mutable_uri_path_normalization_options()->set_skip_path_normalization(true);
  config.mutable_uri_path_normalization_options()->set_skip_merging_slashes(true);
  config.mutable_uri_path_normalization_options()->set_path_with_escaped_slashes_action(
      ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig::
          UriPathNormalizationOptions::KEEP_UNCHANGED);
  return config;
}
} // namespace

FakeHttpConnection::FakeHttpConnection(
    FakeUpstream& fake_upstream, SharedConnectionWrapper& shared_connection, Http::CodecType type,
    Event::TestTimeSystem& time_system, uint32_t max_request_headers_kb,
    uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action)
    : FakeConnectionBase(shared_connection, time_system), type_(type),
      header_validator_factory_(
          IntegrationUtil::makeHeaderValidationFactory(fakeUpstreamHeaderValidatorConfig())) {
  ASSERT(max_request_headers_count != 0);
  if (type == Http::CodecType::HTTP1) {
    Http::Http1Settings http1_settings;
    http1_settings.use_balsa_parser_ =
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http1_use_balsa_parser");
    // For the purpose of testing, we always have the upstream encode the trailers if any
    http1_settings.enable_trailers_ = true;
    Http::Http1::CodecStats& stats = fake_upstream.http1CodecStats();
    codec_ = std::make_unique<TestHttp1ServerConnectionImpl>(
        shared_connection_.connection(), stats, *this, http1_settings, max_request_headers_kb,
        max_request_headers_count, headers_with_underscores_action, overload_manager_);
  } else if (type == Http::CodecType::HTTP2) {
    envoy::config::core::v3::Http2ProtocolOptions http2_options = fake_upstream.http2Options();
    Http::Http2::CodecStats& stats = fake_upstream.http2CodecStats();
    codec_ = std::make_unique<TestHttp2ServerConnectionImpl>(
        shared_connection_.connection(), *this, stats, random_, http2_options,
        max_request_headers_kb, max_request_headers_count, headers_with_underscores_action,
        overload_manager_);
  } else {
    ASSERT(type == Http::CodecType::HTTP3);
#ifdef ENVOY_ENABLE_QUIC
    Http::Http3::CodecStats& stats = fake_upstream.http3CodecStats();
    codec_ = std::make_unique<Quic::QuicHttpServerConnectionImpl>(
        dynamic_cast<Quic::EnvoyQuicServerSession&>(shared_connection_.connection()), *this, stats,
        fake_upstream.http3Options(), max_request_headers_kb, max_request_headers_count,
        headers_with_underscores_action);
#else
    ASSERT(false, "running a QUIC integration test without compiling QUIC");
#endif
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

AssertionResult FakeConnectionBase::close(Network::ConnectionCloseType close_type,
                                          std::chrono::milliseconds timeout) {
  ENVOY_LOG(trace, "FakeConnectionBase close type={}", static_cast<int>(close_type));
  if (!shared_connection_.connected()) {
    return AssertionSuccess();
  }
  return shared_connection_.executeOnDispatcher(
      [&close_type](Network::Connection& connection) { connection.close(close_type); }, timeout);
}

AssertionResult FakeConnectionBase::readDisable(bool disable, std::chrono::milliseconds timeout) {
  return shared_connection_.executeOnDispatcher(
      [disable](Network::Connection& connection) { connection.readDisable(disable); }, timeout);
}

namespace {
Http::Protocol codeTypeToProtocol(Http::CodecType codec_type) {
  switch (codec_type) {
  case Http::CodecType::HTTP1:
    return Http::Protocol::Http11;
  case Http::CodecType::HTTP2:
    return Http::Protocol::Http2;
  case Http::CodecType::HTTP3:
    return Http::Protocol::Http3;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}
} // namespace

Http::ServerHeaderValidatorPtr FakeHttpConnection::makeHeaderValidator() {
  return header_validator_factory_ ? header_validator_factory_->createServerHeaderValidator(
                                         codeTypeToProtocol(type_), header_validator_stats_)
                                   : nullptr;
}

Http::RequestDecoder& FakeHttpConnection::newStream(Http::ResponseEncoder& encoder, bool) {
  absl::MutexLock lock(&lock_);
  new_streams_.emplace_back(new FakeStream(*this, encoder, time_system_));
  return *new_streams_.back();
}

void FakeHttpConnection::onGoAway(Http::GoAwayErrorCode code) {
  ASSERT(type_ != Http::CodecType::HTTP1);
  // Usually indicates connection level errors, no operations are needed since
  // the connection will be closed soon.
  ENVOY_LOG(info, "FakeHttpConnection receives GOAWAY: ", static_cast<int>(code));
}

void FakeHttpConnection::encodeGoAway() {
  ASSERT(type_ != Http::CodecType::HTTP1);

  postToConnectionThread([this]() { codec_->goAway(); });
}

void FakeHttpConnection::updateConcurrentStreams(uint64_t max_streams) {
  ASSERT(type_ != Http::CodecType::HTTP1);

  if (type_ == Http::CodecType::HTTP2) {
    postToConnectionThread([this, max_streams]() {
      auto codec = dynamic_cast<TestHttp2ServerConnectionImpl*>(codec_.get());
      codec->updateConcurrentStreams(max_streams);
    });
  } else {
#ifdef ENVOY_ENABLE_QUIC
    postToConnectionThread([this, max_streams]() {
      auto codec = dynamic_cast<Quic::QuicHttpServerConnectionImpl*>(codec_.get());
      quic::test::QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(
          &codec->quicServerSession(), max_streams);
      codec->quicServerSession().SendMaxStreams(1, false);
    });
#else
    UNREFERENCED_PARAMETER(max_streams);
#endif
  }
}

void FakeHttpConnection::encodeProtocolError() {
  ASSERT(type_ != Http::CodecType::HTTP1);

  Http::Http2::ServerConnectionImpl* codec =
      dynamic_cast<Http::Http2::ServerConnectionImpl*>(codec_.get());
  ASSERT(codec != nullptr);
  postToConnectionThread([codec]() {
    Http::Status status = codec->protocolErrorForTest();
    ASSERT(Http::getStatusCode(status) == Http::StatusCode::CodecProtocolError);
  });
}

AssertionResult FakeConnectionBase::waitForDisconnect(milliseconds timeout) {
  ENVOY_LOG(trace, "FakeConnectionBase waiting for disconnect");
  absl::MutexLock lock(&lock_);
  const auto reached = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    return !shared_connection_.connectedLockHeld();
  };

  if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
    if (timeout == TestUtility::DefaultTimeout) {
      ADD_FAILURE() << "Please don't waitForDisconnect with a 5s timeout if failure is expected\n";
    }
    return AssertionFailure() << "Timed out waiting for disconnect.";
  }
  ENVOY_LOG(trace, "FakeConnectionBase done waiting for disconnect");
  return AssertionSuccess();
}

AssertionResult FakeConnectionBase::waitForRstDisconnect(std::chrono::milliseconds timeout) {
  ENVOY_LOG(trace, "FakeConnectionBase waiting for RST disconnect");
  absl::MutexLock lock(&lock_);
  const auto reached = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    return shared_connection_.rstDisconnected();
  };

  if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
    if (timeout == TestUtility::DefaultTimeout) {
      ADD_FAILURE()
          << "Please don't waitForRstDisconnect with a 5s timeout if failure is expected\n";
    }
    return AssertionFailure() << "Timed out waiting for RST disconnect.";
  }
  ENVOY_LOG(trace, "FakeConnectionBase done waiting for RST disconnect");
  return AssertionSuccess();
}

AssertionResult FakeConnectionBase::waitForHalfClose(milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!time_system_.waitFor(lock_, absl::Condition(&half_closed_), timeout)) {
    return AssertionFailure() << "Timed out waiting for half close.";
  }
  return AssertionSuccess();
}

AssertionResult FakeConnectionBase::waitForNoPost(milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  if (!time_system_.waitFor(
          lock_,
          absl::Condition(
              [](void* fake_connection) -> bool {
                return static_cast<FakeConnectionBase*>(fake_connection)->pending_cbs_ == 0;
              },
              this),
          timeout)) {
    return AssertionFailure() << "Timed out waiting for ops on this connection";
  }
  return AssertionSuccess();
}

void FakeConnectionBase::postToConnectionThread(std::function<void()> cb) {
  ++pending_cbs_;
  dispatcher_.post([this, cb]() {
    cb();
    {
      // Snag this lock not because it's needed but so waitForNoPost doesn't stall
      absl::MutexLock lock(&lock_);
      --pending_cbs_;
    }
  });
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

FakeUpstream::FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                           const std::string& uds_path, const FakeUpstreamConfig& config)
    : FakeUpstream(std::move(transport_socket_factory),
                   Network::SocketPtr{new Network::UdsListenSocket(
                       *Network::Address::PipeInstance::create(uds_path))},
                   config) {}

static Network::SocketPtr
makeTcpListenSocket(const Network::Address::InstanceConstSharedPtr& address) {
  return std::make_unique<Network::TcpListenSocket>(address, nullptr, true);
}

static Network::Address::InstanceConstSharedPtr makeAddress(uint32_t port,
                                                            Network::Address::IpVersion version) {
  return Network::Utility::parseInternetAddressNoThrow(
      Network::Test::getLoopbackAddressString(version), port);
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

static Network::SocketPtr
makeListenSocket(const FakeUpstreamConfig& config,
                 const Network::Address::InstanceConstSharedPtr& address) {
  return (config.udp_fake_upstream_.has_value() ? makeUdpListenSocket(address)
                                                : makeTcpListenSocket(address));
}

FakeUpstream::FakeUpstream(uint32_t port, Network::Address::IpVersion version,
                           const FakeUpstreamConfig& config, const bool defer_initialization)
    : FakeUpstream(Network::Test::createRawBufferDownstreamSocketFactory(),
                   makeListenSocket(config, makeAddress(port, version)), config,
                   defer_initialization) {}

FakeUpstream::FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                           const Network::Address::InstanceConstSharedPtr& address,
                           const FakeUpstreamConfig& config)
    : FakeUpstream(std::move(transport_socket_factory), makeListenSocket(config, address), config) {
}

FakeUpstream::FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                           uint32_t port, Network::Address::IpVersion version,
                           const FakeUpstreamConfig& config)
    : FakeUpstream(std::move(transport_socket_factory),
                   makeListenSocket(config, makeAddress(port, version)), config) {}

FakeUpstream::FakeUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                           Network::SocketPtr&& listen_socket, const FakeUpstreamConfig& config,
                           const bool defer_initialization)
    : http_type_(config.upstream_protocol_), http2_options_(config.http2_options_),
      http3_options_(config.http3_options_), quic_options_(config.quic_options_),
      socket_(Network::SocketSharedPtr(listen_socket.release())),
      api_(Api::createApiForTest(stats_store_)), time_system_(config.time_system_),
      dispatcher_(api_->allocateDispatcher("fake_upstream")),
      handler_(new Server::ConnectionHandlerImpl(*dispatcher_, 0)), config_(config),
      read_disable_on_new_connection_(true), enable_half_close_(config.enable_half_close_),
      listener_(*this, http_type_ == Http::CodecType::HTTP3),
      filter_chain_(Network::Test::createEmptyFilterChain(std::move(transport_socket_factory))),
      stats_scope_(stats_store_.createScope("test_server_scope")) {
  socket_factories_.emplace_back(std::make_unique<FakeListenSocketFactory>(socket_));
  ENVOY_LOG(info, "starting fake server at {}. UDP={} codec={}", localAddress()->asString(),
            config.udp_fake_upstream_.has_value(), FakeHttpConnection::typeToString(http_type_));
  if (config.udp_fake_upstream_.has_value() &&
      config.udp_fake_upstream_->max_rx_datagram_size_.has_value()) {
    listener_.udp_listener_config_.config_.mutable_downstream_socket_config()
        ->mutable_max_rx_datagram_size()
        ->set_value(config.udp_fake_upstream_->max_rx_datagram_size_.value());
  }

  if (!defer_initialization) {
    initializeServer();
  }
}

FakeUpstream::~FakeUpstream() { cleanUp(); };

void FakeUpstream::initializeServer() {
  if (initialized_) {
    // Already initialized.
    return;
  }

  dispatcher_->post([this]() -> void {
    EXPECT_TRUE(socket_factories_[0]->doFinalPreWorkerInit().ok());
    handler_->addListener(absl::nullopt, listener_, runtime_, random_);
    server_initialized_.setReady();
  });
  thread_ = api_->threadFactory().createThread([this]() -> void { threadRoutine(); });
  server_initialized_.waitReady();
  initialized_ = true;
}

void FakeUpstream::cleanUp() {
  if (thread_.get()) {
    dispatcher_->exit();
    thread_->join();
    thread_.reset();
  }
}

bool FakeUpstream::createNetworkFilterChain(Network::Connection& connection,
                                            const Filter::NetworkFilterFactoriesList&) {
  absl::MutexLock lock(&lock_);
  if (read_disable_on_new_connection_ && http_type_ != Http::CodecType::HTTP3) {
    // Disable early close detection to avoid closing the network connection before full
    // initialization is complete.
    connection.detectEarlyCloseWhenReadDisabled(false);
    connection.readDisable(true);
    if (disable_and_do_not_enable_) {
      dynamic_cast<Network::ConnectionImpl*>(&connection)->ioHandle().enableFileEvents(0);
    }
  }
  auto connection_wrapper = std::make_unique<SharedConnectionWrapper>(connection);

  LinkedList::moveIntoListBack(std::move(connection_wrapper), new_connections_);

  // Normally we don't associate a logical network connection with a FakeHttpConnection  until
  // waitForHttpConnection is called, but QUIC needs to be set up as packets come in, so we do
  // not lazily create for HTTP/3
  if (http_type_ == Http::CodecType::HTTP3) {
    quic_connections_.push_back(std::make_unique<FakeHttpConnection>(
        *this, consumeConnection(), http_type_, time_system_, config_.max_request_headers_kb_,
        config_.max_request_headers_count_, config_.headers_with_underscores_action_));
    quic_connections_.back()->initialize();
  }
  return true;
}

bool FakeUpstream::createListenerFilterChain(Network::ListenerFilterManager&) { return true; }

void FakeUpstream::createUdpListenerFilterChain(Network::UdpListenerFilterManager& udp_listener,
                                                Network::UdpReadFilterCallbacks& callbacks) {
  udp_listener.addReadFilter(std::make_unique<FakeUdpFilter>(*this, callbacks));
}

bool FakeUpstream::createQuicListenerFilterChain(Network::QuicListenerFilterManager&) {
  return true;
}

void FakeUpstream::threadRoutine() {
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  handler_.reset();
  {
    absl::MutexLock lock(&lock_);
    new_connections_.clear();
    quic_connections_.clear();
    consumed_connections_.clear();
  }
}

AssertionResult FakeUpstream::waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                                                    FakeHttpConnectionPtr& connection,
                                                    milliseconds timeout) {
  if (!initialized_) {
    return AssertionFailure()
           << "Must initialize the FakeUpstream first by calling initializeServer().";
  }

  {
    absl::MutexLock lock(&lock_);

    // As noted in createNetworkFilterChain, HTTP3 FakeHttpConnections are not
    // lazily created, so HTTP3 needs a different wait path here.
    if (http_type_ == Http::CodecType::HTTP3) {
      if (quic_connections_.empty() &&
          !waitForWithDispatcherRun(
              time_system_, lock_,
              [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return !quic_connections_.empty(); },
              client_dispatcher, timeout)) {
        return AssertionFailure() << "Timed out waiting for new quic connection.";
      }
      if (!quic_connections_.empty()) {
        connection = std::move(quic_connections_.front());
        quic_connections_.pop_front();
        return AssertionSuccess();
      }
    }

    if (!waitForWithDispatcherRun(
            time_system_, lock_,
            [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) { return !new_connections_.empty(); },
            client_dispatcher, timeout)) {
      if (timeout == TestUtility::DefaultTimeout) {
        ADD_FAILURE()
            << "Please don't waitForHttpConnection with a 5s timeout if failure is expected\n";
      }
      return AssertionFailure() << "Timed out waiting for new connection.";
    }
  }
  return runOnDispatcherThreadAndWait([&]() {
    absl::MutexLock lock(&lock_);
    connection = std::make_unique<FakeHttpConnection>(
        *this, consumeConnection(), http_type_, time_system_, config_.max_request_headers_kb_,
        config_.max_request_headers_count_, config_.headers_with_underscores_action_);
    connection->initialize();
    return AssertionSuccess();
  });
}

absl::StatusOr<int>
FakeUpstream::waitForHttpConnection(Event::Dispatcher& client_dispatcher,
                                    std::vector<std::unique_ptr<FakeUpstream>>& upstreams,
                                    FakeHttpConnectionPtr& connection, milliseconds timeout) {
  if (upstreams.empty()) {
    return absl::InternalError("No upstreams configured.");
  }
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (bound.withinBound()) {
    for (size_t i = 0; i < upstreams.size(); ++i) {
      FakeUpstream& upstream = *upstreams[i];
      {
        absl::MutexLock lock(&upstream.lock_);
        if (!upstream.isInitialized()) {
          return absl::InternalError(
              "Must initialize the FakeUpstream first by calling initializeServer().");
        }
        if (!waitForWithDispatcherRun(
                upstream.time_system_, upstream.lock_,
                [&upstream]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(upstream.lock_) {
                  return !upstream.new_connections_.empty();
                },
                client_dispatcher, 5ms)) {
          continue;
        }
      }

      EXPECT_TRUE(upstream.runOnDispatcherThreadAndWait([&]() {
        absl::MutexLock lock(&upstream.lock_);
        connection = std::make_unique<FakeHttpConnection>(
            upstream, upstream.consumeConnection(), upstream.http_type_, upstream.timeSystem(),
            Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
            envoy::config::core::v3::HttpProtocolOptions::ALLOW);
        connection->initialize();
        return AssertionSuccess();
      }));
      return i;
    }
  }
  return absl::InternalError("Timed out waiting for HTTP connection.");
}

ABSL_MUST_USE_RESULT
AssertionResult FakeUpstream::assertPendingConnectionsEmpty() {
  return runOnDispatcherThreadAndWait([&]() {
    absl::MutexLock lock(&lock_);
    return new_connections_.empty() ? AssertionSuccess() : AssertionFailure();
  });
}

AssertionResult FakeUpstream::waitForRawConnection(FakeRawConnectionPtr& connection,
                                                   milliseconds timeout) {
  if (!initialized_) {
    return AssertionFailure()
           << "Must initialize the FakeUpstream first by calling initializeServer().";
  }

  {
    absl::MutexLock lock(&lock_);
    const auto reached = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
      return !new_connections_.empty();
    };

    ENVOY_LOG(debug, "waiting for raw connection");
    if (!time_system_.waitFor(lock_, absl::Condition(&reached), timeout)) {
      return AssertionFailure() << "Timed out waiting for raw connection";
    }
  }

  return runOnDispatcherThreadAndWait([&]() {
    absl::MutexLock lock(&lock_);
    connection = makeRawConnection(consumeConnection(), timeSystem());
    connection->initialize();
    // Skip enableHalfClose if the connection is already disconnected.
    if (connection->connected()) {
      connection->connection().enableHalfClose(enable_half_close_);
    }
    return AssertionSuccess();
  });
}

void FakeUpstream::convertFromRawToHttp(FakeRawConnectionPtr& raw_connection,
                                        FakeHttpConnectionPtr& connection) {
  absl::MutexLock lock(&lock_);
  SharedConnectionWrapper& shared_connection = raw_connection->sharedConnection();

  connection = std::make_unique<FakeHttpConnection>(
      *this, shared_connection, http_type_, time_system_, config_.max_request_headers_kb_,
      config_.max_request_headers_count_, config_.headers_with_underscores_action_);
  connection->initialize();
  raw_connection.release();
}

SharedConnectionWrapper& FakeUpstream::consumeConnection() {
  ASSERT(!new_connections_.empty());
  auto* const connection_wrapper = new_connections_.front().get();
  // Skip the thread safety check if the network connection has already been freed since there's no
  // alternate way to get access to the dispatcher.
  ASSERT(!connection_wrapper->connected() || connection_wrapper->dispatcher().isThreadSafe());
  connection_wrapper->setParented();
  connection_wrapper->moveBetweenLists(new_connections_, consumed_connections_);
  if (read_disable_on_new_connection_ && connection_wrapper->connected() &&
      http_type_ != Http::CodecType::HTTP3 && !disable_and_do_not_enable_) {
    // Re-enable read and early close detection.
    auto& connection = connection_wrapper->connection();
    connection.detectEarlyCloseWhenReadDisabled(true);
    connection.readDisable(false);
  }
  return *connection_wrapper;
}

AssertionResult FakeUpstream::waitForUdpDatagram(Network::UdpRecvData& data_to_fill,
                                                 std::chrono::milliseconds timeout) {
  if (!initialized_) {
    return AssertionFailure()
           << "Must initialize the FakeUpstream first by calling initializeServer().";
  }

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

Network::FilterStatus FakeUpstream::onRecvDatagram(Network::UdpRecvData& data) {
  absl::MutexLock lock(&lock_);
  received_datagrams_.emplace_back(std::move(data));

  return Network::FilterStatus::StopIteration;
}

AssertionResult FakeUpstream::runOnDispatcherThreadAndWait(std::function<AssertionResult()> cb,
                                                           std::chrono::milliseconds timeout) {
  auto result = std::make_shared<AssertionResult>(AssertionSuccess());
  auto done = std::make_shared<absl::Notification>();
  ASSERT(!dispatcher_->isThreadSafe());
  dispatcher_->post([&]() {
    *result = cb();
    done->Notify();
  });
  RELEASE_ASSERT(done->WaitForNotificationWithTimeout(absl::FromChrono(timeout)),
                 "Timed out waiting for cb to run on dispatcher");
  return *result;
}

void FakeUpstream::runOnDispatcherThread(std::function<void()> cb) {
  ASSERT(!dispatcher_->isThreadSafe());
  dispatcher_->post([&]() { cb(); });
}

void FakeUpstream::sendUdpDatagram(const std::string& buffer,
                                   const Network::Address::InstanceConstSharedPtr& peer) {
  dispatcher_->post([this, buffer, peer] {
    const auto rc = Network::Utility::writeToSocket(socket_->ioHandle(), Buffer::OwnedImpl(buffer),
                                                    nullptr, *peer);
    EXPECT_TRUE(rc.return_value_ == buffer.length());
  });
}

AssertionResult FakeUpstream::rawWriteConnection(uint32_t index, const std::string& data,
                                                 bool end_stream,
                                                 std::chrono::milliseconds timeout) {
  if (!initialized_) {
    return AssertionFailure()
           << "Must initialize the FakeUpstream first by calling initializeServer().";
  }

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

absl::Status FakeUpstream::FakeListenSocketFactory::doFinalPreWorkerInit() {
  if (socket_->socketType() == Network::Socket::Type::Stream) {
    EXPECT_EQ(0, socket_->ioHandle().listen(ENVOY_TCP_BACKLOG_SIZE).return_value_);
  } else {
    ASSERT(socket_->socketType() == Network::Socket::Type::Datagram);
    EXPECT_TRUE(Network::Socket::applyOptions(socket_->options(), *socket_,
                                              envoy::config::core::v3::SocketOption::STATE_BOUND));
  }
  return absl::OkStatus();
}

FakeRawConnection::~FakeRawConnection() {
  // If the filter was already deleted, it means the shared_connection_ was too, so don't try to
  // access it.
  if (read_filter_ != nullptr) {
    EXPECT_TRUE(shared_connection_.executeOnDispatcher(
        [filter = std::move(read_filter_)](Network::Connection& connection) {
          connection.removeReadFilter(filter);
        }));
  }
}

void FakeRawConnection::initialize() {
  FakeConnectionBase::initialize();
  read_filter_ = std::make_shared<ReadFilter>(*this);
  if (!shared_connection_.connected()) {
    ENVOY_LOG(warn, "FakeRawConnection::initialize: network connection is already disconnected");
    return;
  }
  ASSERT(shared_connection_.dispatcher().isThreadSafe());
  shared_connection_.connection().addReadFilter(read_filter_);
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

ABSL_MUST_USE_RESULT
AssertionResult FakeHttpConnection::waitForInexactRawData(const char* data, std::string* out,
                                                          std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&lock_);
  const auto reached = [this, data, &out]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    char peek_buf[200];
    auto result = dynamic_cast<Network::ConnectionImpl*>(&connection())
                      ->ioHandle()
                      .recv(peek_buf, 200, MSG_PEEK);
    ASSERT(result.ok() || result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again);
    if (!result.ok()) {
      return false;
    }
    absl::string_view peek_data(peek_buf, result.return_value_);
    size_t index = peek_data.find(data);
    if (index != absl::string_view::npos) {
      Buffer::OwnedImpl buffer;
      *out = std::string(peek_data.data(), index + 4);
      auto result = dynamic_cast<Network::ConnectionImpl*>(&connection())
                        ->ioHandle()
                        .recv(peek_buf, index + 4, 0);
      return true;
    }
    return false;
  };
  // Because the connection must be read disabled to not auto-consume the
  // underlying data, waitFor hangs with no events to force the time system to
  // continue. Break it up into smaller chunks.
  for (int i = 0; i < timeout / 10ms; ++i) {
    if (time_system_.waitFor(lock_, absl::Condition(&reached), 10ms)) {
      return AssertionSuccess();
    }
  }
  return AssertionFailure() << "timed out waiting for raw data";
}

void FakeHttpConnection::writeRawData(absl::string_view data) {
  Buffer::OwnedImpl buffer(data);
  Api::IoCallUint64Result result =
      dynamic_cast<Network::ConnectionImpl*>(&connection())->ioHandle().write(buffer);
  ASSERT(result.ok());
}

AssertionResult FakeHttpConnection::postWriteRawData(std::string data) {
  return shared_connection_.executeOnDispatcher(
      [data](Network::Connection& connection) {
        Buffer::OwnedImpl to_write(data);
        connection.write(to_write, false);
      },
      TestUtility::DefaultTimeout);
}

} // namespace Envoy
