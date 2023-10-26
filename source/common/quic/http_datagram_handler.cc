#include "source/common/quic/http_datagram_handler.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/common/capsule.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Quic {

HttpDatagramHandler::HttpDatagramHandler(quic::QuicSpdyStream& stream) : stream_(stream) {
  stream_.RegisterHttp3DatagramVisitor(this);
}

HttpDatagramHandler::~HttpDatagramHandler() { stream_.UnregisterHttp3DatagramVisitor(); }

void HttpDatagramHandler::decodeCapsule(const quiche::Capsule& capsule) {
  quiche::QuicheBuffer serialized_capsule = SerializeCapsule(capsule, &capsule_buffer_allocator_);
  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add(serialized_capsule.AsStringView());
  if (!stream_decoder_) {
    IS_ENVOY_BUG("HTTP/3 Datagram received before a stream decoder is set.");
  }
  stream_decoder_->decodeData(*buffer, stream_.IsDoneReading());
}

void HttpDatagramHandler::OnHttp3Datagram(quic::QuicStreamId stream_id, absl::string_view payload) {
  ASSERT(stream_id == stream_.id());
  ENVOY_LOG(debug, "received a HTTP/3 Datagram with stream id={}", stream_id);
  decodeCapsule(quiche::Capsule::Datagram(payload));
}

void HttpDatagramHandler::OnUnknownCapsule(quic::QuicStreamId stream_id,
                                           const quiche::UnknownCapsule& capsule) {
  ASSERT(stream_id == stream_.id());
  ENVOY_LOG(debug, "received an Unknown Capsule with stream id={}", stream_id);
  decodeCapsule(quiche::Capsule(capsule));
}

bool HttpDatagramHandler::OnCapsule(const quiche::Capsule& capsule) {
  quiche::CapsuleType capsule_type = capsule.capsule_type();
  if (capsule_type != quiche::CapsuleType::DATAGRAM) {
    // Forward other types of Capsules without modifications.
    stream_.WriteCapsule(capsule, fin_set_);
    return true;
  }
  quic::MessageStatus status =
      stream_.SendHttp3Datagram(capsule.datagram_capsule().http_datagram_payload);
  if (status == quic::MessageStatus::MESSAGE_STATUS_SUCCESS) {
    return true;
  }
  // When SendHttp3Datagram cannot send a datagram immediately, it puts it into the queue and
  // returns MESSAGE_STATUS_BLOCKED.
  if (status == quic::MessageStatus::MESSAGE_STATUS_BLOCKED) {
    ENVOY_LOG(trace, fmt::format("SendHttpH3Datagram failed: status = {}, buffers the Datagram.",
                                 quic::MessageStatusToString(status)));
    return true;
  }
  if (status == quic::MessageStatus::MESSAGE_STATUS_TOO_LARGE) {
    ENVOY_LOG(warn, fmt::format("SendHttpH3Datagram failed: status = {}, drops the Datagram.",
                                quic::MessageStatusToString(status)));
    return true;
  }
  // Otherwise, returns false and thus resets the corresponding stream.
  ENVOY_LOG(error, fmt::format("SendHttpH3Datagram failed: status = {}, resets the stream.",
                               quic::MessageStatusToString(status)));
  return false;
}

void HttpDatagramHandler::OnCapsuleParseFailure(absl::string_view error_message) {
  ENVOY_LOG(error, fmt::format("Capsule parsing failed: error_message = {}", error_message));
}

bool HttpDatagramHandler::encodeCapsuleFragment(absl::string_view capsule_fragment,
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
