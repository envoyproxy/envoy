#pragma once

#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/io_socket/user_space/io_handle.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace InternalUpstream {

#define ALL_INTERNAL_UPSTREAM_STATS(COUNTER) COUNTER(no_metadata)

/**
 * Struct definition for all internal transport socket stats. @see stats_macros.h
 */
struct InternalUpstreamStats {
  ALL_INTERNAL_UPSTREAM_STATS(GENERATE_COUNTER_STRUCT)
};

class Config : public Logger::Loggable<Logger::Id::upstream> {
public:
  Config(
      const envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport&
          config_proto,
      Stats::Scope& scope);
  std::unique_ptr<envoy::config::core::v3::Metadata>
  extractMetadata(const Upstream::HostDescriptionConstSharedPtr& host) const;

private:
  enum class MetadataKind { Host, Cluster };
  struct MetadataSource {
    MetadataSource(MetadataKind kind, const std::string& name) : kind_(kind), name_(name) {}
    const MetadataKind kind_;
    const std::string name_;
  };
  InternalUpstreamStats stats_;
  std::vector<MetadataSource> metadata_sources_;
};

class InternalSocketFactory : public PassthroughFactory {
public:
  InternalSocketFactory(
      Server::Configuration::TransportSocketFactoryContext& context,
      const envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport&
          config_proto,
      Network::UpstreamTransportSocketFactoryPtr&& inner_factory);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;

private:
  const Config config_;
};

} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
