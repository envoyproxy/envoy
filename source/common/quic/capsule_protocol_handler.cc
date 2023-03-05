#include "source/common/quic/capsule_protocol_handler.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Quic {

void CapsuleProtocolHandler::OnHttp3Datagram(quic::QuicStreamId stream_id,
                                             absl::string_view payload) {
  ASSERT(stream_id == stream_->id());
  ENVOY_LOG(debug, "received HTTP/3 Datagram with stream id={}", stream_id);

  quiche::Capsule capsule = quiche::Capsule::Datagram(payload);
  quiche::QuicheBuffer serialized_capsule = SerializeCapsule(capsule, &capsule_buffer_allocator_);

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add(serialized_capsule.AsStringView());

  ASSERT(stream_decoder_);
  stream_decoder_->decodeData(*buffer, stream_->IsDoneReading());
}

bool CapsuleProtocolHandler::OnCapsule(const quiche::Capsule& capsule) {
  quiche::CapsuleType capsule_type = capsule.capsule_type();
  // TODO(jeongseokson): Check if we should support legacy Datagram types.
  if (capsule_type != quiche::CapsuleType::DATAGRAM) {
    ENVOY_LOG(error, fmt::format("Capsule type is invalid: capsule_type = {}",
                                 static_cast<uint64_t>(capsule_type)));
    return false;
  }
  quic::MessageStatus status =
      stream_->SendHttp3Datagram(capsule.datagram_capsule().http_datagram_payload);
  if (status != quic::MessageStatus::MESSAGE_STATUS_SUCCESS) {
    ENVOY_LOG(error, fmt::format("SendHttpH3Datagram failed: status = {}",
                                 quic::MessageStatusToString(status)));
    return false;
  }
  return true;
}

void CapsuleProtocolHandler::OnCapsuleParseFailure(absl::string_view error_message) {
  ENVOY_LOG(error, fmt::format("Capsule parsing failed: error_message = {}", error_message));
}

bool CapsuleProtocolHandler::encodeCapsule(absl::string_view capsule_data) {
  // If a CapsuleParser object fails to parse a capsule fragment, it cannot be used again. Thus, a
  // new one is created for parsing every time.
  quiche::CapsuleParser capsule_parser{this};
  if (!capsule_parser.IngestCapsuleFragment(capsule_data)) {
    ENVOY_LOG(error,
              fmt::format("Capsule parsing error occured: capsule_fragment = {}", capsule_data));
    return false;
  }
  return true;
}

void CapsuleProtocolHandler::onHeaders(Http::HeaderMapImpl* const headers) {
  Http::HeaderMap::GetResult capsule_protocol =
      headers->get(Envoy::Http::LowerCaseString("Capsule-Protocol"));
  if (!capsule_protocol.empty() && capsule_protocol[0]->value().getStringView() == "?1") {
    stream_->RegisterHttp3DatagramVisitor(this);
    using_capsule_protocol_ = true;
  }
}

void CapsuleProtocolHandler::onStreamClosed() {
  if (using_capsule_protocol_) {
    stream_->UnregisterHttp3DatagramVisitor();
    using_capsule_protocol_ = false;
  }
}

} // namespace Quic
} // namespace Envoy
