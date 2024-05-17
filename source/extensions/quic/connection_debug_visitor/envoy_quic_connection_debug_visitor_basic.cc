#include "source/extensions/quic/connection_debug_visitor/envoy_quic_connection_debug_visitor_basic.h"

#include <memory>

#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"

#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"

namespace Envoy {
namespace Quic {

void EnvoyQuicConnectionDebugVisitorBasic::OnConnectionClosed(
    const quic::QuicConnectionCloseFrame& frame, quic::ConnectionCloseSource source) {
  ENVOY_LOG(info, "Quic connection from {} with id {} closed {} with details: {}",
            session_->peer_address().ToString(), session_->connection_id().ToString(),
            quic::ConnectionCloseSourceToString(source), frame.error_details);
}

std::unique_ptr<quic::QuicConnectionDebugVisitor>
EnvoyQuicConnectionDebugVisitorFactoryBasic::createQuicConnectionDebugVisitor(
    quic::QuicSession* session, const StreamInfo::StreamInfo& stream_info) {
  return std::make_unique<EnvoyQuicConnectionDebugVisitorBasic>(session, stream_info);
}

REGISTER_FACTORY(EnvoyQuicConnectionDebugVisitorFactoryBasic,
                 EnvoyQuicConnectionDebugVisitorFactoryInterface);

} // namespace Quic
} // namespace Envoy
