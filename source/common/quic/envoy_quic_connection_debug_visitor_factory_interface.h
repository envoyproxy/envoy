#pragma once

#include <memory>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/process_context.h"
#include "envoy/stream_info/stream_info.h"

#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_session.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicConnectionDebugVisitorFactoryInterface : public Config::TypedFactory {
public:
  std::string category() const override { return "envoy.quic.connection_debug_visitor"; }

  void setContext(Envoy::ProcessContextOptRef context) { context_ = context; }

  // Returns a debug visitor to be attached to a Quic Connection.
  virtual std::unique_ptr<quic::QuicConnectionDebugVisitor>
  createQuicConnectionDebugVisitor(quic::QuicSession* session,
                                   const StreamInfo::StreamInfo& stream_info) PURE;

protected:
  Envoy::ProcessContextOptRef context_;
};

using EnvoyQuicConnectionDebugVisitorFactoryInterfaceOptRef =
    OptRef<EnvoyQuicConnectionDebugVisitorFactoryInterface>;

} // namespace Quic
} // namespace Envoy
