#include "source/extensions/upstreams/http/udp/upstream_request.h"

#include <cstdint>
#include <memory>

#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/router.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

#include "quiche/common/masque/connect_udp_datagram_payload.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

void UdpConnPool::newStream(Router::GenericConnectionPoolCallbacks* callbacks) {
  Envoy::Network::SocketPtr socket = createSocket(host_);
  auto source_address_selector = host_->cluster().getUpstreamLocalAddressSelector();
  auto upstream_local_address = source_address_selector->getUpstreamLocalAddress(
      host_->address(), /*socket_options=*/nullptr);
  if (!Envoy::Network::Socket::applyOptions(upstream_local_address.socket_options_, *socket,
                                            envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
    callbacks->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                             "Failed to apply socket option for UDP upstream", host_);
    return;
  }
  if (upstream_local_address.address_) {
    Envoy::Api::SysCallIntResult bind_result = socket->bind(upstream_local_address.address_);
    if (bind_result.return_value_ < 0) {
      callbacks->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                               "Failed to bind for UDP upstream", host_);
      return;
    }
  }

  const Network::ConnectionInfoProvider& connection_info_provider =
      socket->connectionInfoProvider();
  Router::UpstreamToDownstream& upstream_to_downstream = callbacks->upstreamToDownstream();
  ASSERT(upstream_to_downstream.connection().has_value());
  Event::Dispatcher& dispatcher = upstream_to_downstream.connection()->dispatcher();
  auto upstream =
      std::make_unique<UdpUpstream>(&upstream_to_downstream, std::move(socket), host_, dispatcher);
  StreamInfo::StreamInfoImpl stream_info(dispatcher.timeSource(), nullptr);

  callbacks->onPoolReady(std::move(upstream), host_, connection_info_provider, stream_info, {});
}

UdpUpstream::UdpUpstream(Router::UpstreamToDownstream* upstream_to_downstream,
                         Network::SocketPtr socket, Upstream::HostConstSharedPtr host,
                         Event::Dispatcher& dispatcher)
    : upstream_to_downstream_(upstream_to_downstream), socket_(std::move(socket)), host_(host),
      dispatcher_(dispatcher) {
  socket_->ioHandle().initializeFileEvent(
      dispatcher_, [this](uint32_t) { onSocketReadReady(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
}

void UdpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    absl::string_view mem_slice(static_cast<const char*>(slice.mem_), slice.len_);
    if (!capsule_parser_.IngestCapsuleFragment(mem_slice)) {
      ENVOY_LOG_MISC(error, "Capsule ingestion error occured: slice = {}", mem_slice);
      break;
    }
  }
  if (end_stream) {
    capsule_parser_.ErrorIfThereIsRemainingBufferedData();
  }
}

Envoy::Http::Status UdpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap& /*headers*/,
                                               bool end_stream) {
  // For successful CONNECT-UDP handshakes, synthesizes the 200 response headers downstream.
  Envoy::Http::ResponseHeaderMapPtr response_headers{
      Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Status, "200"},
           {Envoy::Http::Headers::get().CapsuleProtocol, "?1"}})};
  if (end_stream) {
    // If the request header is the end of the stream, responds with 400 Bad Request. Does not
    // return an error code to avoid replying with 503 Service Unavailable.
    response_headers->setStatus("400");
    response_headers->remove(Envoy::Http::Headers::get().CapsuleProtocol);
    upstream_to_downstream_->onResetStream(Envoy::Http::StreamResetReason::ConnectError, "");
  } else {
    Api::SysCallIntResult rc = socket_->connect(host_->address());
    if (SOCKET_FAILURE(rc.return_value_)) {
      return absl::InternalError("Upstream socket connect failure.");
    }
  }
  // Indicates the end of stream for the subsequent filters in the chain.
  upstream_to_downstream_->decodeHeaders(std::move(response_headers), end_stream);
  return Envoy::Http::okStatus();
}

void UdpUpstream::resetStream() {
  upstream_to_downstream_ = nullptr;
  socket_->close();
}

void UdpUpstream::onSocketReadReady() {
  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Network::Utility::readPacketsFromSocket(
      socket_->ioHandle(), *socket_->connectionInfoProvider().localAddress(), *this,
      dispatcher_.timeSource(), /*prefer_gro=*/true, packets_dropped);
  if (result == nullptr) {
    socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    return;
  }
}

// The local and peer addresses are not used in this method since the socket is already bound and
// connected to the upstream server in the encodeHeaders method.
void UdpUpstream::processPacket(Network::Address::InstanceConstSharedPtr /*local_address*/,
                                Network::Address::InstanceConstSharedPtr /*peer_address*/,
                                Buffer::InstancePtr buffer, MonotonicTime /*receive_time*/) {
  std::string data = buffer->toString();
  quiche::ConnectUdpDatagramUdpPacketPayload payload(data);
  quiche::QuicheBuffer serialized_capsule =
      SerializeCapsule(quiche::Capsule::Datagram(payload.Serialize()), &capsule_buffer_allocator_);

  Buffer::InstancePtr capsule_data = std::make_unique<Buffer::OwnedImpl>();
  capsule_data->add(serialized_capsule.AsStringView());
  bytes_meter_->addWireBytesReceived(capsule_data->length());
  upstream_to_downstream_->decodeData(*capsule_data, /*end_stream=*/false);
}

bool UdpUpstream::OnCapsule(const quiche::Capsule& capsule) {
  quiche::CapsuleType capsule_type = capsule.capsule_type();
  if (capsule_type != quiche::CapsuleType::DATAGRAM) {
    // Silently drops capsules with an unknown type.
    return true;
  }

  std::unique_ptr<quiche::ConnectUdpDatagramPayload> connect_udp_datagram_payload =
      quiche::ConnectUdpDatagramPayload::Parse(capsule.datagram_capsule().http_datagram_payload);
  if (!connect_udp_datagram_payload) {
    // Indicates parsing failure to reset the data stream.
    return false;
  }

  if (connect_udp_datagram_payload->GetType() !=
      quiche::ConnectUdpDatagramPayload::Type::kUdpPacket) {
    // Silently drops Datagrams with an unknown Context ID.
    return true;
  }

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add(connect_udp_datagram_payload->GetUdpProxyingPayload());
  bytes_meter_->addWireBytesSent(buffer->length());
  Api::IoCallUint64Result rc = Network::Utility::writeToSocket(
      socket_->ioHandle(), *buffer, /*local_ip=*/nullptr, *host_->address());
  // TODO(https://github.com/envoyproxy/envoy/issues/23564): Handle some socket errors here.
  return true;
}

void UdpUpstream::OnCapsuleParseFailure(absl::string_view error_message) {
  upstream_to_downstream_->onResetStream(Envoy::Http::StreamResetReason::ProtocolError,
                                         error_message);
}

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
