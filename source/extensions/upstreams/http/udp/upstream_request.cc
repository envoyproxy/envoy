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
  // TODO: If the data is in the Capsule format, parses it and extracts the UDP proxying payload.
  bytes_meter_->addWireBytesSent(data.length());
  Api::IoCallUint64Result rc = Network::Utility::writeToSocket(
      socket_->ioHandle(), data, /*local_ip=*/nullptr, *host_->address());
  // TODO: Error Statistics
}

Envoy::Http::Status UdpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap&,
                                               bool /*end_stream*/) {
  Api::SysCallIntResult rc = socket_->connect(host_->address());
  if (SOCKET_FAILURE(rc.return_value_)) {
    //TODO: statistics.
    return absl::InternalError("Upstream socket connect failure.");
  }
  // Synthesize the 200 response headers downstream to complete the CONNECT-UDP handshake.
  Envoy::Http::ResponseHeaderMapPtr headers{
      Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Status, "200"}})};
  upstream_request_->decodeHeaders(std::move(headers), false);
  return Envoy::Http::okStatus();
}

void UdpUpstream::resetStream() { upstream_request_ = nullptr; }

void UdpUpstream::onSocketReadReady() {
  uint32_t packets_dropped = 0;
  socket_->connectionInfoProvider().dumpState(std::cout, 2);
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
  // TODO: Converts data to a HTTP Datagram.
  bytes_meter_->addWireBytesReceived(buffer->length());
  upstream_request_->decodeData(*buffer, false);
}

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
