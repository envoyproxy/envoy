#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/internal/v3/internal_upstream.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

class Config {
public:
  Config(const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport& config_proto,
         Stats::Scope& scope);
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class InternalSocket : public TransportSockets::PassthroughSocket,
                       Logger::Loggable<Logger::Id::connection> {
public:
  InternalSocket(ConfigConstSharedPtr config, Network::TransportSocketPtr inner_socket);

private:
  const ConfigConstSharedPtr config_;
  const std::map<std::string, ProtobufWkt::Struct> injected_metadata_;
};

} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
