#include "envoy/http/codec.h"

#include "common/common/assert.h"

#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

namespace Envoy {
namespace Quic {

// QuicHttpConnectionImplBase instance is a thin QUIC codec just providing quic interface to HCM.
// Owned by HCM and created during onNewConnection() if the network connection
// is a QUIC connection.
class QuicHttpConnectionImplBase : public virtual Http::Connection {
public:
  QuicHttpConnectionImplBase(quic::QuicSpdySession& quic_session) : quic_session_(quic_session) {}

  // Http::Connection
  void dispatch(Buffer::Instance& /*data*/) override {
    // Bypassed. QUIC connection already hands all data to streams.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  Http::Protocol protocol() override {
    // No need to distinguish QUIC from H2 from HCM's view so far.
    return Http::Protocol::Http2;
  }
  void goAway() override;
  bool wantsToWrite() override;
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override {
    // Data in L4 buffers is managed by QUIC.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

private:
  quic::QuicSpdySession& quic_session_;
};

class QuicHttpServerConnectionImpl : public QuicHttpConnectionImplBase,
                                     public Http::ServerConnection {
public:
  QuicHttpServerConnectionImpl(EnvoyQuicServerSession& quic_session,
                               Http::ServerConnectionCallbacks& callbacks)
      : QuicHttpConnectionImplBase(quic_session) {
    quic_session.setHttpConnectionCallbacks(callbacks);
  }

  // Http::Connection
  void shutdownNotice() override {
    // TODO(danzh): Add double-GOAWAY support in QUIC.
  }
};

} // namespace Quic
} // namespace Envoy
