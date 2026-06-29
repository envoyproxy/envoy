#pragma once

#include <cstdint>

#include "source/common/http/http2/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

/**
 * HTTP/2 client codec subclass that can emit a graceful "draining" GOAWAY from the client side.
 *
 * The stock Http::Connection::shutdownNotice() maps to nghttp2/oghttp2 SubmitShutdownNotice(),
 * which both libraries treat as server-only -- it is a no-op on a client session. To send the
 * graceful first GOAWAY (last-stream-id = 2^31-1, NO_ERROR) from the upstream/client side we call
 * SubmitGoAway() directly, which is valid for both roles. The final GOAWAY (the real
 * last-stream-id) is the inherited ConnectionImpl::goAway().
 *
 * This relies on ConnectionImpl::adapter_ and sendPendingFramesAndHandleError() being protected
 * (and ClientConnectionImpl deriving publicly from ConnectionImpl), so no core change is needed.
 */
class DrainAwareHttp2ClientConnection : public Envoy::Http::Http2::ClientConnectionImpl {
public:
  using Envoy::Http::Http2::ClientConnectionImpl::ClientConnectionImpl;

  // Phase 1 of a graceful drain: GOAWAY with the max sentinel stream id (no error).
  void sendGracefulGoAway() {
    static constexpr int32_t MaxStreamId = 0x7fffffff; // 2^31 - 1
    ENVOY_LOG(info,
              "reverse_tunnel upstream codec: submitting graceful GOAWAY last_stream_id={:#x}",
              MaxStreamId);
    adapter_->SubmitGoAway(MaxStreamId, http2::adapter::Http2ErrorCode::HTTP2_NO_ERROR, "");
    if (sendPendingFramesAndHandleError()) {
      return;
    }
  }
};

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
