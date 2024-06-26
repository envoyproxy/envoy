#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/quic/connection_debug_visitor/v3/connection_debug_visitor_basic.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"

#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"

namespace Envoy {
namespace Quic {

// Visitor class that writes connection-level information to info logs.
class EnvoyQuicConnectionDebugVisitorBasic : public quic::QuicConnectionDebugVisitor,
                                             private Logger::Loggable<Logger::Id::connection> {
public:
  EnvoyQuicConnectionDebugVisitorBasic(quic::QuicSession* session,
                                       const StreamInfo::StreamInfo& stream_info)
      : session_(session), stream_info_(stream_info) {
    // Workaround for gcc not understanding [[maybe_unused]] for class members.
    (void)stream_info_;
  }

  // Writes peer address, connection ID, close source, and error details to info logs.
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;

private:
  quic::QuicSession* session_;
  const StreamInfo::StreamInfo& stream_info_;
};

class EnvoyQuicConnectionDebugVisitorFactoryBasic
    : public EnvoyQuicConnectionDebugVisitorFactoryInterface {
public:
  std::string name() const override { return "envoy.quic.connection_debug_visitor.basic"; }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::quic::connection_debug_visitor::v3::BasicConfig>();
  }

  std::unique_ptr<quic::QuicConnectionDebugVisitor>
  createQuicConnectionDebugVisitor(quic::QuicSession* session,
                                   const StreamInfo::StreamInfo& stream_info) override;
};

DECLARE_FACTORY(EnvoyQuicConnectionDebugVisitorFactoryBasic);

} // namespace Quic
} // namespace Envoy
