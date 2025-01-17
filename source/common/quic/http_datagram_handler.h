#pragma once

#include "envoy/http/codec.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"

namespace Envoy {
namespace Quic {

// Enables HTTP Datagrams and the Capsule Protocol support based on RFC 9297. This class is used
// both on the decode path (HTTP/3 codec -> Envoy) and the encode path (Envoy -> HTTP/3 codec) of
// the QUIC client and server streams in Envoy.
class HttpDatagramHandler : public quic::QuicSpdyStream::Http3DatagramVisitor,
                            public quiche::CapsuleParser::Visitor,
                            protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  // Does not take ownership of the QuicSpdyStream. "stream" must refer to a valid object
  // that outlives HttpDatagramHandler.
  explicit HttpDatagramHandler(quic::QuicSpdyStream& stream);

  // quic::QuicSpdyStream::Http3DatagramVisitor
  void OnHttp3Datagram(quic::QuicStreamId stream_id, absl::string_view payload) override;
  void OnUnknownCapsule(quic::QuicStreamId stream_id,
                        const quiche::UnknownCapsule& capsule) override;

  // quiche::CapsuleParser::Visitor
  // Converts a given Capsule to a HTTP/3 Datagrams and send it.
  bool OnCapsule(const quiche::Capsule& capsule) override;
  void OnCapsuleParseFailure(absl::string_view error_message) override;

  // Re-encodes given Capsule fragment into an HTTP/3 Datagram. Returns true if an HTTP/3 Datagram
  // created and sent successfully. Returns false if the CapsuleParser fails to parse the
  // |capsule_fragment| or the corresponding stream fails to send the Datagram.
  bool encodeCapsuleFragment(absl::string_view capsule_fragment, bool end_stream);
  // Does not take ownership of the StreamDecoder. "decoder" must outlive HttpDatagramHandler.
  void setStreamDecoder(Http::StreamDecoder* decoder) { stream_decoder_ = decoder; }

private:
  // Serializes and decodes a given Capsule.
  void decodeCapsule(const quiche::Capsule& capsule);

  quic::QuicSpdyStream& stream_;
  quiche::CapsuleParser capsule_parser_{this};
  quiche::SimpleBufferAllocator capsule_buffer_allocator_;
  Http::StreamDecoder* stream_decoder_ = nullptr; // not owned.
  bool fin_set_ = false;
};

} // namespace Quic
} // namespace Envoy
