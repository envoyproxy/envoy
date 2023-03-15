#include "source/common/quic/capsule_protocol_handler.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Quic {

CapsuleProtocolHandler::CapsuleProtocolHandler(quic::QuicSpdyStream* stream) : stream_(stream) {
  ASSERT(stream != nullptr);
  stream_->RegisterHttp3DatagramVisitor(this);
}

CapsuleProtocolHandler::~CapsuleProtocolHandler() {
  if (stream_ != nullptr) {
    stream_->UnregisterHttp3DatagramVisitor();
  }
}

void CapsuleProtocolHandler::decodeCapsule(const quiche::Capsule& capsule) {
  quiche::QuicheBuffer serialized_capsule = SerializeCapsule(capsule, &capsule_buffer_allocator_);
  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add(serialized_capsule.AsStringView());
  if (!stream_decoder_) {
    IS_ENVOY_BUG("HTTP/3 Datagram received before a stream decoder is set.");
  }
  stream_decoder_->decodeData(*buffer, stream_->IsDoneReading());
}

void CapsuleProtocolHandler::OnHttp3Datagram(quic::QuicStreamId stream_id,
                                             absl::string_view payload) {
  ASSERT(stream_id == stream_->id());
  ENVOY_LOG(debug, "received a HTTP/3 Datagram with stream id={}", stream_id);
  decodeCapsule(quiche::Capsule::Datagram(payload));
}

void CapsuleProtocolHandler::OnUnknownCapsule(quic::QuicStreamId stream_id,
                                              const quiche::UnknownCapsule& capsule) {
  ASSERT(stream_id == stream_->id());
  ENVOY_LOG(debug, "received an Unknown Capsule with stream id={}", stream_id);
  decodeCapsule(quiche::Capsule(capsule));
}

bool CapsuleProtocolHandler::OnCapsule(const quiche::Capsule& capsule) {
  quiche::CapsuleType capsule_type = capsule.capsule_type();
  if (capsule_type != quiche::CapsuleType::DATAGRAM) {
    // Forward other types of Capsules without modifications.
    stream_->WriteCapsule(capsule, fin_set_);
    return true;
  }
  quic::MessageStatus status =
      stream_->SendHttp3Datagram(capsule.datagram_capsule().http_datagram_payload);
  // Returns false and thus resets the corresponding stream in the following statuses. Otherwise,
  // drops the Datagram and move on without reporting a failure.
  if (status == quic::MessageStatus::MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED ||
      status == quic::MessageStatus::MESSAGE_STATUS_UNSUPPORTED ||
      status == quic::MessageStatus::MESSAGE_STATUS_INTERNAL_ERROR) {
    ENVOY_LOG(error, fmt::format("SendHttpH3Datagram failed: status = {}",
                                 quic::MessageStatusToString(status)));
    return false;
  }
  return true;
}

void CapsuleProtocolHandler::OnCapsuleParseFailure(absl::string_view error_message) {
  ENVOY_LOG(error, fmt::format("Capsule parsing failed: error_message = {}", error_message));
}

bool CapsuleProtocolHandler::encodeCapsuleFragment(absl::string_view capsule_fragment,
                                                   bool end_stream) {
  fin_set_ = end_stream;
  // If a CapsuleParser object fails to parse a capsule fragment, the corresponding stream should
  // be reset. Returning false in this method resets the stream.
  if (!capsule_parser_.IngestCapsuleFragment(capsule_fragment)) {
    ENVOY_LOG(error, fmt::format("Capsule parsing error occured: capsule_fragment = {}",
                                 capsule_fragment));
    return false;
  }
  return true;
}

} // namespace Quic
} // namespace Envoy
