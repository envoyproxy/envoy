#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/internal/v3/internal_upstream.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/host_description.h"

#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

class Config {
public:
  Config(const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport&
             config_proto,
         Stats::Scope& scope);
  envoy::config::core::v3::Metadata
  extractMetadata(Upstream::HostDescriptionConstSharedPtr host) const;

private:
  enum class MetadataKind { Host, Cluster };
  struct MetadataSource {
    MetadataSource(MetadataKind kind, const std::string& name) : kind_(kind), name_(name) {}
    const MetadataKind kind_;
    const std::string name_;
  };
  absl::flat_hash_map<std::string, MetadataSource> metadata_sources_;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class InternalSocket : public TransportSockets::PassthroughSocket,
                       Logger::Loggable<Logger::Id::connection> {
public:
  InternalSocket(ConfigConstSharedPtr config, Network::TransportSocketPtr inner_socket,
                 Upstream::HostDescriptionConstSharedPtr host);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;

private:
  const envoy::config::core::v3::Metadata injected_metadata_;
};

} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
