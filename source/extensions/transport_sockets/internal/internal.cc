#include "source/extensions/transport_sockets/internal/internal.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/io_socket/user_space/io_handle.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

Config::Config(const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport&
                   config_proto,
               Stats::Scope&) {
  for (const auto& metadata : config_proto.passthrough_metadata()) {
    MetadataKind kind;
    switch (metadata.kind().kind_case()) {
    case envoy::type::metadata::v3::MetadataKind::KindCase::kHost:
      kind = MetadataKind::Host;
      break;
    case envoy::type::metadata::v3::MetadataKind::KindCase::kCluster:
      kind = MetadataKind::Cluster;
      break;
    default:
      throw EnvoyException("Metadata type is not supported");
    }
    metadata_sources_.push_back(MetadataSource(kind, metadata.name()));
  }
}
envoy::config::core::v3::Metadata
Config::extractMetadata(Upstream::HostDescriptionConstSharedPtr host) const {
  envoy::config::core::v3::Metadata metadata;
  if (!host) {
    return metadata;
  }
  for (const auto& source : metadata_sources_) {
    switch (source.kind_) {
    case MetadataKind::Host: {
      if (host->metadata()->filter_metadata().contains(source.name_)) {
        (*metadata.mutable_filter_metadata())[source.name_] =
            host->metadata()->filter_metadata().at(source.name_);
      }
      break;
    }
    case MetadataKind::Cluster: {
      if (host->cluster().metadata().filter_metadata().contains(source.name_)) {
        (*metadata.mutable_filter_metadata())[source.name_] =
            host->cluster().metadata().filter_metadata().at(source.name_);
      }
      break;
    }
    default:
      PANIC("not reached");
    }
  }
  return metadata;
}

InternalSocket::InternalSocket(ConfigConstSharedPtr config,
                               Network::TransportSocketPtr inner_socket,
                               Upstream::HostDescriptionConstSharedPtr host)
    : PassthroughSocket(std::move(inner_socket)),
      injected_metadata_(config->extractMetadata(host)) {}

void InternalSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  // downcast to user space socket
  auto* handle = dynamic_cast<IoSocket::UserSpace::IoHandle*>(&callbacks.ioHandle());
  if (handle != nullptr) {
    handle->setMetadata(injected_metadata_);
  }
}

} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
