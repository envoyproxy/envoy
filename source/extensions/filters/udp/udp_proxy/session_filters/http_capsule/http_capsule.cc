#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/http_capsule.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"

#include "absl/strings/escaping.h"
#include "quiche/common/masque/connect_udp_datagram_payload.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {

ReadFilterStatus HttpCapsuleFilter::onData(Network::UdpRecvData& data) {
  std::string buffer = data.buffer_->toString();
  quiche::ConnectUdpDatagramUdpPacketPayload payload(buffer);
  quiche::QuicheBuffer serialized_capsule =
      SerializeCapsule(quiche::Capsule::Datagram(payload.Serialize()), &capsule_buffer_allocator_);

  data.buffer_->drain(data.buffer_->length());
  data.buffer_->add(serialized_capsule.AsStringView());
  return ReadFilterStatus::Continue;
}

WriteFilterStatus HttpCapsuleFilter::onWrite(Network::UdpRecvData& data) {
  // TODO(ohadvano): add filter callbacks to get addresses instead of saving them.
  local_address_ = data.addresses_.local_;
  peer_address_ = data.addresses_.peer_;

  for (const Buffer::RawSlice& slice : data.buffer_->getRawSlices()) {
    absl::string_view mem_slice(reinterpret_cast<const char*>(slice.mem_), slice.len_);
    if (!capsule_parser_.IngestCapsuleFragment(mem_slice)) {
      ENVOY_LOG(error, "Capsule ingestion error occured: slice length = {}", slice.len_);
      break;
    }
  }

  // We always stop here as OnCapsule() callback will be responsible to inject
  // datagrams to the filter chain once they are ready.
  data.buffer_->drain(data.buffer_->length());
  return WriteFilterStatus::StopIteration;
}

bool HttpCapsuleFilter::OnCapsule(const quiche::Capsule& capsule) {
  quiche::CapsuleType capsule_type = capsule.capsule_type();
  if (capsule_type != quiche::CapsuleType::DATAGRAM) {
    // Silently drops capsules with an unknown type.
    return true;
  }

  std::unique_ptr<quiche::ConnectUdpDatagramPayload> connect_udp_datagram_payload =
      quiche::ConnectUdpDatagramPayload::Parse(capsule.datagram_capsule().http_datagram_payload);
  if (!connect_udp_datagram_payload) {
    // Indicates parsing failure to reset the data stream.
    ENVOY_LOG(debug, "capsule parsing error");
    return false;
  }

  if (connect_udp_datagram_payload->GetType() !=
      quiche::ConnectUdpDatagramPayload::Type::kUdpPacket) {
    // Silently drops Datagrams with an unknown Context ID.
    return true;
  }

  Network::UdpRecvData datagram;
  datagram.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  datagram.buffer_->add(connect_udp_datagram_payload->GetUdpProxyingPayload());
  datagram.receive_time_ = time_source_.monotonicTime();
  datagram.addresses_ = {local_address_, peer_address_};

  write_callbacks_->injectDatagramToFilterChain(datagram);
  return true;
}

void HttpCapsuleFilter::OnCapsuleParseFailure(absl::string_view reason) {
  ENVOY_LOG(debug, "capsule parse failure: {}", reason);
}

} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
