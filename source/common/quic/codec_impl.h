#pragma once

#include "envoy/http/codec.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

// QuicHttpConnectionImplBase instance is a thin QUIC codec just providing quic interface to HCM.
// Owned by HCM and created during onNewConnection() if the network connection
// is a QUIC connection.
class QuicHttpConnectionImplBase : public virtual Http::Connection,
                                   protected Logger::Loggable<Logger::Id::quic> {
public:
  QuicHttpConnectionImplBase(QuicFilterManagerConnectionImpl& quic_session,
                             Http::Http3::CodecStats& stats)
      : quic_session_(quic_session), stats_(stats) {}

  // Http::Connection
  Http::Status dispatch(Buffer::Instance& /*data*/) override {
    PANIC("not implemented"); // QUIC connection already hands all data to streams.
  }
  Http::Protocol protocol() override { return Http::Protocol::Http3; }
  // Returns true if the session has data to send but queued in connection or
  // stream send buffer.
  bool wantsToWrite() override { return quic_session_.bytesToSend() > 0; }

protected:
  QuicFilterManagerConnectionImpl& quic_session_;
  Http::Http3::CodecStats& stats_;
};

} // namespace Quic
} // namespace Envoy
