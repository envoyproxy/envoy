#include "utility.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/connection.h"

#include "source/common/api/api_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/quic_client_transport_socket_factory.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#endif

#ifdef ENVOY_ENABLE_YAML
#include "test/common/upstream/utility.h"
#include "test/integration/ssl_utility.h"
#endif
#include "test/mocks/common.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"

namespace Envoy {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;
using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::ClientHttp1HeaderValidator;
using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::ClientHttp2HeaderValidator;
using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::ConfigOverrides;
using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::ServerHttp1HeaderValidator;
using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::ServerHttp2HeaderValidator;

namespace {

RawConnectionDriver::DoWriteCallback writeBufferCallback(Buffer::Instance& data) {
  auto shared_data = std::make_shared<Buffer::OwnedImpl>();
  shared_data->move(data);
  return [shared_data](Buffer::Instance& dest) {
    if (shared_data->length() > 0) {
      dest.add(*shared_data);
      shared_data->drain(shared_data->length());
    }
    return false;
  };
}

} // namespace

void BufferingStreamDecoder::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  headers_ = std::move(headers);
  if (complete_) {
    onComplete();
  }
}

void BufferingStreamDecoder::decodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!complete_);
  complete_ = end_stream;
  body_.append(data.toString());
  if (complete_) {
    onComplete();
  }
}

void BufferingStreamDecoder::decodeTrailers(Http::ResponseTrailerMapPtr&&) {
  PANIC("not implemented");
}

void BufferingStreamDecoder::onComplete() {
  ASSERT(complete_);
  on_complete_cb_();
}

void BufferingStreamDecoder::onResetStream(Http::StreamResetReason, absl::string_view) {
  ADD_FAILURE();
}

// A callback for a QUIC client connection to unblock the test after handshake succeeds. QUIC
// network connection initiates handshake and raises Connected event when it's done. Tests should
// proceed with sending requests afterwards.
class TestConnectionCallbacks : public Network::ConnectionCallbacks {
public:
  TestConnectionCallbacks(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::Connected) {
      // Handshake finished, unblock the test to continue. This is needed because we call
      // Dispatcher::run() with Block to wait for the handshake to finish before proceeding.
      // TODO(danzh) find an alternative approach with behaviors more in parallel with SSL.
      connected_ = true;
      dispatcher_.exit();
    } else if (event == Network::ConnectionEvent::RemoteClose) {
      // If the peer closes the connection, no need to wait anymore.
      dispatcher_.exit();
    } else {
      if (!connected_) {
        // Before handshake gets established, any connection failure should exit the loop. I.e. a
        // QUIC connection may fail of INVALID_VERSION if both this client doesn't support any of
        // the versions the server advertised before handshake established. In this case the
        // connection is closed locally and this is in a blocking event loop.
        dispatcher_.exit();
      }
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Event::Dispatcher& dispatcher_;
  bool connected_{false};
};

Network::UpstreamTransportSocketFactoryPtr
IntegrationUtil::createQuicUpstreamTransportSocketFactory(Api::Api& api, Stats::Store& store,
                                                          Ssl::ContextManager& context_manager,
                                                          ThreadLocal::Instance& threadlocal,
                                                          const std::string& san_to_match,
                                                          bool connect_to_upstreams) {
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context;
  ON_CALL(context.server_context_, api()).WillByDefault(testing::ReturnRef(api));
  ON_CALL(context, statsScope()).WillByDefault(testing::ReturnRef(*store.rootScope()));
  ON_CALL(context.server_context_, sslContextManager())
      .WillByDefault(testing::ReturnRef(context_manager));
  ON_CALL(context.server_context_, threadLocal()).WillByDefault(testing::ReturnRef(threadlocal));
  envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport
      quic_transport_socket_config;
  auto* tls_context = quic_transport_socket_config.mutable_upstream_tls_context();
#ifdef ENVOY_ENABLE_YAML
  initializeUpstreamTlsContextConfig(
      Ssl::ClientSslTransportOptions().setAlpn(true).setSan(san_to_match).setSni("lyft.com"),
      *tls_context, connect_to_upstreams);
#else
  UNREFERENCED_PARAMETER(tls_context);
  UNREFERENCED_PARAMETER(san_to_match);
  UNREFERENCED_PARAMETER(connect_to_upstreams);
  RELEASE_ASSERT(0, "unsupported");
#endif // ENVOY_ENABLE_YAML

  envoy::config::core::v3::TransportSocket message;
  message.mutable_typed_config()->PackFrom(quic_transport_socket_config);
  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(message);
  return config_factory.createTransportSocketFactory(quic_transport_socket_config, context).value();
}

BufferingStreamDecoderPtr
sendRequestAndWaitForResponse(Event::Dispatcher& dispatcher, const std::string& method,
                              const std::string& url, const std::string& body,
                              const std::string& host, const std::string& content_type,
                              Http::CodecClientProd& client) {
  BufferingStreamDecoderPtr response(new BufferingStreamDecoder([&]() -> void {
    client.close();
    dispatcher.exit();
  }));
  Http::RequestEncoder& encoder = client.newStream(*response);
  encoder.getStream().addCallbacks(*response);

  Http::TestRequestHeaderMapImpl headers;
  headers.setMethod(method);
  headers.setPath(url);
  headers.setHost(host);
  headers.setReferenceScheme(Http::Headers::get().SchemeValues.Http);
  if (!content_type.empty()) {
    headers.setContentType(content_type);
  }
  const auto status = encoder.encodeHeaders(headers, body.empty());
  ASSERT(status.ok());
  if (!body.empty()) {
    Buffer::OwnedImpl body_buffer(body);
    encoder.encodeData(body_buffer, true);
  }

  dispatcher.run(Event::Dispatcher::RunType::Block);
  return response;
}

BufferingStreamDecoderPtr
IntegrationUtil::makeSingleRequest(const Network::Address::InstanceConstSharedPtr& addr,
                                   const std::string& method, const std::string& url,
                                   const std::string& body, Http::CodecType type,
                                   const std::string& host, const std::string& content_type) {
  NiceMock<Stats::MockIsolatedStatsStore> mock_stats_store;
  Quic::QuicStatNames quic_stat_names(mock_stats_store.symbolTable());
  NiceMock<Random::MockRandomGenerator> random;
  Event::GlobalTimeSystem time_system;
  NiceMock<Random::MockRandomGenerator> random_generator;
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  Api::Impl api(Thread::threadFactoryForTest(), mock_stats_store, time_system,
                Filesystem::fileSystemForTest(), random_generator, bootstrap);
  Event::DispatcherPtr dispatcher(api.allocateDispatcher("test_thread"));
  TestConnectionCallbacks connection_callbacks(*dispatcher);
  Network::TransportSocketOptionsConstSharedPtr options;

  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_description =
      std::shared_ptr<Upstream::HostDescriptionImpl>(*Upstream::HostDescriptionImpl::create(
          cluster, "",
          *Network::Utility::resolveUrl(
              fmt::format("{}://127.0.0.1:80", (type == Http::CodecType::HTTP3 ? "udp" : "tcp"))),
          nullptr, nullptr, std::make_shared<envoy::config::core::v3::Locality>(),
          envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0));

  if (type <= Http::CodecType::HTTP2) {
    Http::CodecClientProd client(type,
                                 dispatcher->createClientConnection(
                                     addr, Network::Address::InstanceConstSharedPtr(),
                                     Network::Test::createRawBufferSocket(), nullptr, nullptr),
                                 host_description, *dispatcher, random, options);
    return sendRequestAndWaitForResponse(*dispatcher, method, url, body, host, content_type,
                                         client);
  }

#ifdef ENVOY_ENABLE_QUIC
  testing::NiceMock<ThreadLocal::MockInstance> threadlocal;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  Extensions::TransportSockets::Tls::ContextManagerImpl manager(server_factory_context);
  Network::UpstreamTransportSocketFactoryPtr transport_socket_factory =
      createQuicUpstreamTransportSocketFactory(api, mock_stats_store, manager, threadlocal,
                                               "spiffe://lyft.com/backend-team");
  auto& quic_transport_socket_factory =
      dynamic_cast<Quic::QuicClientTransportSocketFactory&>(*transport_socket_factory);
  std::unique_ptr<Quic::PersistentQuicInfoImpl> persistent_info =
      Quic::createPersistentQuicInfoForCluster(*dispatcher, *cluster);

  Network::Address::InstanceConstSharedPtr local_address;
  if (addr->ip()->version() == Network::Address::IpVersion::v4) {
    local_address = Network::Utility::getLocalAddress(Network::Address::IpVersion::v4);
  } else {
    // Docker only works with loopback v6 address.
    local_address = std::make_shared<Network::Address::Ipv6Instance>("::1");
  }
  quic::DeterministicConnectionIdGenerator generator(quic::kQuicDefaultConnectionIdLength);
  Network::ClientConnectionPtr connection = Quic::createQuicNetworkConnection(
      *persistent_info, quic_transport_socket_factory.getCryptoConfig(),
      quic::QuicServerId(
          quic_transport_socket_factory.clientContextConfig()->serverNameIndication(),
          static_cast<uint16_t>(addr->ip()->port())),
      *dispatcher, addr, local_address, quic_stat_names, {}, *mock_stats_store.rootScope(), nullptr,
      nullptr, generator, quic_transport_socket_factory);
  connection->addConnectionCallbacks(connection_callbacks);
  Http::CodecClientProd client(type, std::move(connection), host_description, *dispatcher, random,
                               options);
  // Quic connection needs to finish handshake.
  dispatcher->run(Event::Dispatcher::RunType::Block);
  return sendRequestAndWaitForResponse(*dispatcher, method, url, body, host, content_type, client);
#else
  ASSERT(false, "running a QUIC integration test without compiling QUIC");
  return nullptr;
#endif
}

BufferingStreamDecoderPtr
IntegrationUtil::makeSingleRequest(uint32_t port, const std::string& method, const std::string& url,
                                   const std::string& body, Http::CodecType type,
                                   Network::Address::IpVersion ip_version, const std::string& host,
                                   const std::string& content_type) {
  auto addr = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(ip_version), port));
  return makeSingleRequest(addr, method, url, body, type, host, content_type);
}

RawConnectionDriver::RawConnectionDriver(uint32_t port, Buffer::Instance& request_data,
                                         ReadCallback response_data_callback,
                                         Network::Address::IpVersion version,
                                         Event::Dispatcher& dispatcher,
                                         Network::TransportSocketPtr transport_socket)
    : RawConnectionDriver(port, writeBufferCallback(request_data), response_data_callback, version,
                          dispatcher, std::move(transport_socket)) {}

RawConnectionDriver::RawConnectionDriver(uint32_t port, DoWriteCallback write_request_callback,
                                         ReadCallback response_data_callback,
                                         Network::Address::IpVersion version,
                                         Event::Dispatcher& dispatcher,
                                         Network::TransportSocketPtr transport_socket)
    : dispatcher_(dispatcher), remaining_bytes_to_send_(0) {
  api_ = Api::createApiForTest(stats_store_);
  Event::GlobalTimeSystem time_system;
  callbacks_ = std::make_unique<ConnectionCallbacks>(
      [this, write_request_callback]() {
        Buffer::OwnedImpl buffer;
        const bool close_after = write_request_callback(buffer);
        remaining_bytes_to_send_ += buffer.length();
        client_->write(buffer, close_after);
      },
      dispatcher);

  if (transport_socket == nullptr) {
    transport_socket = Network::Test::createRawBufferSocket();
  }

  client_ = dispatcher_.createClientConnection(
      *Network::Utility::resolveUrl(
          fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version), port)),
      Network::Address::InstanceConstSharedPtr(), std::move(transport_socket), nullptr, nullptr);
  // ConnectionCallbacks will call write_request_callback from the connect and low-watermark
  // callbacks. Set a small buffer limit so high-watermark is triggered after every write and
  // low-watermark is triggered every time the buffer is drained.
  client_->setBufferLimits(1);
  client_->addConnectionCallbacks(*callbacks_);
  client_->addReadFilter(
      Network::ReadFilterSharedPtr{new ForwardingFilter(*this, response_data_callback)});
  client_->addBytesSentCallback([&](uint64_t bytes) {
    remaining_bytes_to_send_ -= bytes;
    return true;
  });
  client_->connect();
}

// This factory is needed to avoid dealing with the ServerFactoryContext, which is
// problematic in dedicated threads for fake upstreams or test client.
// UHV is needed in fake upstreams or test client for translation
// of extended CONNECT to upgrade, which is done by the codecs.
class FakeHeaderValidatorFactory : public Http::HeaderValidatorFactory {
public:
  FakeHeaderValidatorFactory(const HeaderValidatorConfig& config) : config_(config) {}

  Http::ServerHeaderValidatorPtr
  createServerHeaderValidator(Http::Protocol protocol, Http::HeaderValidatorStats& stats) override {
    ConfigOverrides config_overrides;

    switch (protocol) {
    case Http::Protocol::Http3:
    case Http::Protocol::Http2:
      return std::make_unique<ServerHttp2HeaderValidator>(config_, protocol, stats,
                                                          config_overrides);
    case Http::Protocol::Http11:
    case Http::Protocol::Http10:
      return std::make_unique<ServerHttp1HeaderValidator>(config_, protocol, stats,
                                                          config_overrides);
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  Http::ClientHeaderValidatorPtr
  createClientHeaderValidator(Http::Protocol protocol, Http::HeaderValidatorStats& stats) override {
    ConfigOverrides config_overrides;

    switch (protocol) {
    case Http::Protocol::Http3:
    case Http::Protocol::Http2:
      return std::make_unique<ClientHttp2HeaderValidator>(config_, protocol, stats,
                                                          config_overrides);
    case Http::Protocol::Http11:
    case Http::Protocol::Http10:
      return std::make_unique<ClientHttp1HeaderValidator>(config_, protocol, stats,
                                                          config_overrides);
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

private:
  const HeaderValidatorConfig config_;
};

Http::HeaderValidatorFactoryPtr
IntegrationUtil::makeHeaderValidationFactory([[maybe_unused]] const HeaderValidatorConfig& config) {
#ifdef ENVOY_ENABLE_UHV
  return std::make_unique<FakeHeaderValidatorFactory>(config);
#else
  return nullptr;
#endif
}

RawConnectionDriver::~RawConnectionDriver() = default;

testing::AssertionResult RawConnectionDriver::waitForConnection() {
  // TODO(mattklein123): Add a timeout and switch to events and waitFor().
  while (!callbacks_->connected() && !callbacks_->closed()) {
    Event::GlobalTimeSystem().timeSystem().realSleepDoNotUseWithoutScrutiny(
        std::chrono::milliseconds(10));
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  }
  if (!callbacks_->connected()) {
    return testing::AssertionFailure();
  }
  return testing::AssertionSuccess();
}

testing::AssertionResult RawConnectionDriver::run(Event::Dispatcher::RunType run_type,
                                                  std::chrono::milliseconds timeout) {
  Event::TimerPtr timeout_timer = dispatcher_.createTimer([this]() -> void { dispatcher_.exit(); });
  timeout_timer->enableTimer(timeout);

  dispatcher_.run(run_type);

  if (timeout_timer->enabled()) {
    timeout_timer->disableTimer();
    return testing::AssertionSuccess();
  }
  return testing::AssertionFailure();
}

void RawConnectionDriver::close() { client_->close(Network::ConnectionCloseType::FlushWrite); }

bool RawConnectionDriver::allBytesSent() const { return remaining_bytes_to_send_ == 0; }

WaitForPayloadReader::WaitForPayloadReader(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

Network::FilterStatus WaitForPayloadReader::onData(Buffer::Instance& data, bool end_stream) {
  data_.append(data.toString());
  data.drain(data.length());
  read_end_stream_ = end_stream;
  if ((!data_to_wait_for_.empty() && absl::StartsWith(data_, data_to_wait_for_)) ||
      (exact_match_ == false && data_.find(data_to_wait_for_) != std::string::npos) || end_stream) {
    data_to_wait_for_.clear();
    dispatcher_.exit();
  }

  if (wait_for_length_ && data_.size() >= length_to_wait_for_) {
    wait_for_length_ = false;
    dispatcher_.exit();
  }

  return Network::FilterStatus::StopIteration;
}

} // namespace Envoy
