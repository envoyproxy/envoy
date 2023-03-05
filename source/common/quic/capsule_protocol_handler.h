#pragma once

#include "envoy/http/codec.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Quic {

class CapsuleProtocolHandler : public quic::QuicSpdyStream::Http3DatagramVisitor,
                               public quiche::CapsuleParser::Visitor,
                               protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  // Does not take ownership of the QuicSpdyStream. "stream" must refer to a valid object
  // that outlives CapsuleProtocolHandler.
  explicit CapsuleProtocolHandler(quic::QuicSpdyStream* stream) : stream_(stream) {}

  // quic::QuicSpdyStream::Http3DatagramVisitor
  // Normalizes Http3 Datagrams to Capsules for Envoy to do subsequent processing.
  void OnHttp3Datagram(quic::QuicStreamId stream_id, absl::string_view payload) override;

  // quiche::CapsuleParser::Visitor
  // Converts a given Capsule to a Http3 Datagrams and send it.
  bool OnCapsule(const quiche::Capsule& capsule) override;
  void OnCapsuleParseFailure(absl::string_view error_message) override;

  // Re-encodes a given Capsule data into an Http3 Datagram. Returns true if the encoding is
  // successful, otherwise false.
  bool encodeCapsule(absl::string_view capsule_data);
  void setStreamDecoder(Http::StreamDecoder* decoder) { stream_decoder_ = decoder; }
  // Checks if the Capsule-Protocol header field is present in the headers to enable the
  // Capsule-Protocol.
  void onHeaders(Http::HeaderMapImpl* const headers);
  void onStreamClosed();
  bool usingCapsuleProtocol() { return using_capsule_protocol_; };

private:
  quic::QuicSpdyStream* const stream_; // not owned, can't be null.
  quiche::SimpleBufferAllocator capsule_buffer_allocator_;
  Http::StreamDecoder* stream_decoder_ = nullptr; // not owned.
  bool using_capsule_protocol_ = false;
};

} // namespace Quic
} // namespace Envoy
