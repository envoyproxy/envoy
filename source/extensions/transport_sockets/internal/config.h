#pragma once

#include "envoy/extensions/transport_sockets/internal/v3/internal_upstream.pb.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/io_socket/user_space/io_handle.h"
#include "source/extensions/transport_sockets/common/passthrough.h"
#include "source/extensions/transport_sockets/internal/internal.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

class Config {
public:
  Config(const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport&
             config_proto,
         Stats::Scope& scope);
  std::unique_ptr<envoy::config::core::v3::Metadata>
  extractMetadata(const Upstream::HostDescriptionConstSharedPtr& host) const;
  std::unique_ptr<IoSocket::UserSpace::FilterStateObjects>
  extractFilterState(const StreamInfo::FilterStateSharedPtr& filter_state) const;

private:
  enum class MetadataKind { Host, Cluster };
  struct MetadataSource {
    MetadataSource(MetadataKind kind, const std::string& name) : kind_(kind), name_(name) {}
    const MetadataKind kind_;
    const std::string name_;
  };
  std::vector<MetadataSource> metadata_sources_;
  std::vector<std::string> filter_state_names_;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class InternalSocketFactory : public PassthroughFactory {
public:
  InternalSocketFactory(
      Server::Configuration::TransportSocketFactoryContext& context,
      const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport& config,
      Network::TransportSocketFactoryPtr&& inner_factory);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options) const override;

private:
  ConfigConstSharedPtr config_;
};

} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
