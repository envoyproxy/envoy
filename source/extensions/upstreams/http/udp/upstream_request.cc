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
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

void UdpConnPool::newStream(Router::GenericConnectionPoolCallbacks* callbacks) {
  Router::UpstreamToDownstream& upstream_request = callbacks->upstreamToDownstream();
  ASSERT(upstream_request.connection().has_value());
  Network::SocketPtr socket = createSocket(host_);
  const Network::ConnectionInfoProvider& connection_info_provider =
      socket->connectionInfoProvider();
  Event::Dispatcher& dispatcher = upstream_request.connection()->dispatcher();
  auto upstream =
      std::make_unique<UdpUpstream>(&upstream_request, std::move(socket), host_, dispatcher);
  StreamInfo::StreamInfoImpl stream_info(dispatcher.timeSource(), nullptr);
  callbacks->onPoolReady(std::move(upstream), host_, connection_info_provider, stream_info, {});
}

UdpUpstream::UdpUpstream(Router::UpstreamToDownstream* upstream_request, Network::SocketPtr socket,
                         Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher)
    : upstream_request_(upstream_request), socket_(std::move(socket)), host_(host),
      dispatcher_(dispatcher) {
  socket_->ioHandle().initializeFileEvent(
      dispatcher_, [this](uint32_t) { onSocketReadReady(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
}

void UdpUpstream::encodeData(Buffer::Instance& data, bool /*end_stream*/) {
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    absl::string_view mem_slice(static_cast<const char*>(slice.mem_), slice.len_);
    if (!capsule_parser_.IngestCapsuleFragment(mem_slice)) {
      ENVOY_LOG_MISC(error, "Capsule ingestion error occured: slice = {}", mem_slice);
      break;
    }
  }
  capsule_parser_.ErrorIfThereIsRemainingBufferedData();
}

Envoy::Http::Status UdpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap&,
                                               bool /*end_stream*/) {
  Api::SysCallIntResult rc = socket_->connect(host_->address());
  if (SOCKET_FAILURE(rc.return_value_)) {
    // TODO: statistics.
    return absl::InternalError("Upstream socket connect failure.");
  }
  // Synthesize the 200 response headers downstream to complete the CONNECT-UDP handshake.
  Envoy::Http::ResponseHeaderMapPtr headers{
      Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Status, "200"},
           {Envoy::Http::Headers::get().CapsuleProtocol, "?1"}})};
  upstream_request_->decodeHeaders(std::move(headers), false);
  return Envoy::Http::okStatus();
}

void UdpUpstream::resetStream() { upstream_request_ = nullptr; }

void UdpUpstream::onSocketReadReady() {
  uint32_t packets_dropped = 0;
  const Api::IoErrorPtr result = Network::Utility::readPacketsFromSocket(
      socket_->ioHandle(), *socket_->connectionInfoProvider().localAddress(), *this,
      dispatcher_.timeSource(), /*prefer_gro=*/false, packets_dropped);
  if (result == nullptr) {
    socket_->ioHandle().activateFileEvents(Event::FileReadyType::Read);
    return;
  }
  if (result->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    // TODO: Increment receive error count
  }

  // TODO: Check with mattklein if flushing is necessary.
}

void UdpUpstream::processPacket(Network::Address::InstanceConstSharedPtr,
                                Network::Address::InstanceConstSharedPtr,
                                Buffer::InstancePtr buffer, MonotonicTime) {
  std::string data = buffer->toString();
  quiche::QuicheBuffer serialized_capsule =
      SerializeCapsule(quiche::Capsule::Datagram(data), &capsule_buffer_allocator_);

  Buffer::InstancePtr capsule_data = std::make_unique<Buffer::OwnedImpl>();
  capsule_data->add(serialized_capsule.AsStringView());
  bytes_meter_->addWireBytesReceived(capsule_data->length());
  upstream_request_->decodeData(*capsule_data, false);
}

bool UdpUpstream::OnCapsule(const quiche::Capsule& capsule) {
  quiche::CapsuleType capsule_type = capsule.capsule_type();
  if (capsule_type != quiche::CapsuleType::DATAGRAM) {
    // Silently drops Datagram Capsules with an unknown type.
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
  // TODO: Error Statistics
  return true;
}

void UdpUpstream::OnCapsuleParseFailure(absl::string_view error_message) {
  upstream_request_->onResetStream(Envoy::Http::StreamResetReason::ProtocolError, error_message);
}

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
