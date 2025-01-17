#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_session.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicConnectionDebugVisitorFactoryInterface {
public:
  virtual ~EnvoyQuicConnectionDebugVisitorFactoryInterface() = default;

  // Returns a debug visitor to be attached to a Quic Connection.
  virtual std::unique_ptr<quic::QuicConnectionDebugVisitor>
  createQuicConnectionDebugVisitor(Event::Dispatcher& dispatcher, quic::QuicSession& session,
                                   const StreamInfo::StreamInfo& stream_info) PURE;
};

using EnvoyQuicConnectionDebugVisitorFactoryInterfacePtr =
    std::unique_ptr<EnvoyQuicConnectionDebugVisitorFactoryInterface>;
using EnvoyQuicConnectionDebugVisitorFactoryInterfaceOptRef =
    OptRef<EnvoyQuicConnectionDebugVisitorFactoryInterface>;

class EnvoyQuicConnectionDebugVisitorFactoryFactoryInterface : public Config::TypedFactory {
public:
  std::string category() const override { return "envoy.quic.connection_debug_visitor"; }

  virtual EnvoyQuicConnectionDebugVisitorFactoryInterfacePtr
  createFactory(const Protobuf::Message& config,
                Server::Configuration::ListenerFactoryContext& listener_context) PURE;
};

} // namespace Quic
} // namespace Envoy
