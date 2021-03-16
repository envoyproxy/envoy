#include "utility.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/connection.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http3/quic_client_connection_factory.h"
#include "common/http/http3/well_known_names.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
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
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void BufferingStreamDecoder::onComplete() {
  ASSERT(complete_);
  on_complete_cb_();
}

void BufferingStreamDecoder::onResetStream(Http::StreamResetReason, absl::string_view) {
  ADD_FAILURE();
}

struct ConnectionCallbacks : public Network::ConnectionCallbacks {
  ConnectionCallbacks(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::Connected) {
      connected_ = true;
      dispatcher_.exit();
    } else if (event == Network::ConnectionEvent::RemoteClose) {
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

  Event::Dispatcher& dispatcher_;
  bool connected_{false};
};

Network::TransportSocketFactoryPtr IntegrationUtil::createQuicClientTransportSocketFactory(
    Server::Configuration::TransportSocketFactoryContext& context,
    const std::string& san_to_match) {
  envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport
      quic_transport_socket_config;
  auto* tls_context = quic_transport_socket_config.mutable_upstream_tls_context();
  initializeUpstreamTlsContextConfig(
      Ssl::ClientSslTransportOptions().setAlpn(true).setSan(san_to_match).setSni("lyft.com"),
      *tls_context);

  envoy::config::core::v3::TransportSocket message;
  message.mutable_typed_config()->PackFrom(quic_transport_socket_config);
  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(message);
  return config_factory.createTransportSocketFactory(quic_transport_socket_config, context);
}

BufferingStreamDecoderPtr
IntegrationUtil::makeSingleRequest(const Network::Address::InstanceConstSharedPtr& addr,
                                   const std::string& method, const std::string& url,
                                   const std::string& body, Http::CodecClient::Type type,
                                   const std::string& host, const std::string& content_type) {
  NiceMock<Stats::MockIsolatedStatsStore> mock_stats_store;
  NiceMock<Random::MockRandomGenerator> random;
  Event::GlobalTimeSystem time_system;
  NiceMock<Random::MockRandomGenerator> random_generator;
  Api::Impl api(Thread::threadFactoryForTest(), mock_stats_store, time_system,
                Filesystem::fileSystemForTest(), random_generator);
  Event::DispatcherPtr dispatcher(api.allocateDispatcher("test_thread"));
  ConnectionCallbacks connection_callbacks(*dispatcher);
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
      cluster,
      fmt::format("{}://127.0.0.1:80", (type == Http::CodecClient::Type::HTTP3 ? "udp" : "tcp")),
      time_system)};

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx, api()).WillByDefault(testing::ReturnRef(api));
  Network::TransportSocketFactoryPtr transport_socket_factory =
      createQuicClientTransportSocketFactory(mock_factory_ctx, "spiffe://lyft.com/backend-team");
  std::unique_ptr<Http::PersistentQuicInfo> persistent_info;
  Network::ClientConnectionPtr connection;
  if (type == Http::CodecClient::Type::HTTP3) {
    Http::QuicClientConnectionFactory& connection_factory =
        Config::Utility::getAndCheckFactoryByName<Http::QuicClientConnectionFactory>(
            Http::QuicCodecNames::get().Quiche);
    persistent_info = connection_factory.createNetworkConnectionInfo(
        *dispatcher, *transport_socket_factory, mock_stats_store, time_system, addr);
    connection = connection_factory.createQuicNetworkConnection(
        *persistent_info, *dispatcher, addr, Network::Address::InstanceConstSharedPtr());
    connection->addConnectionCallbacks(connection_callbacks);
  } else {
    connection =
        dispatcher->createClientConnection(addr, Network::Address::InstanceConstSharedPtr(),
                                           Network::Test::createRawBufferSocket(), nullptr);
  }
  Http::CodecClientProd client(type, std::move(connection), host_description, *dispatcher, random);
  if (type == Http::CodecClient::Type::HTTP3) {
    // Quic connection needs to finish handshake.
    dispatcher->run(Event::Dispatcher::RunType::Block);
  }

  BufferingStreamDecoderPtr response(new BufferingStreamDecoder([&]() -> void {
    client.close();
    dispatcher->exit();
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

  dispatcher->run(Event::Dispatcher::RunType::Block);
  return response;
}

BufferingStreamDecoderPtr
IntegrationUtil::makeSingleRequest(uint32_t port, const std::string& method, const std::string& url,
                                   const std::string& body, Http::CodecClient::Type type,
                                   Network::Address::IpVersion ip_version, const std::string& host,
                                   const std::string& content_type) {
  auto addr = Network::Utility::resolveUrl(
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
  callbacks_ = std::make_unique<ConnectionCallbacks>([this, write_request_callback]() {
    Buffer::OwnedImpl buffer;
    const bool close_after = write_request_callback(buffer);
    remaining_bytes_to_send_ += buffer.length();
    client_->write(buffer, close_after);
  });

  if (transport_socket == nullptr) {
    transport_socket = Network::Test::createRawBufferSocket();
  }

  client_ = dispatcher_.createClientConnection(
      Network::Utility::resolveUrl(
          fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version), port)),
      Network::Address::InstanceConstSharedPtr(), std::move(transport_socket), nullptr);
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

RawConnectionDriver::~RawConnectionDriver() = default;

void RawConnectionDriver::waitForConnection() {
  // TODO(mattklein123): Add a timeout and switch to events and waitFor().
  while (!callbacks_->connected() && !callbacks_->closed()) {
    Event::GlobalTimeSystem().timeSystem().realSleepDoNotUseWithoutScrutiny(
        std::chrono::milliseconds(10));
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  }
}

void RawConnectionDriver::run(Event::Dispatcher::RunType run_type) { dispatcher_.run(run_type); }

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
