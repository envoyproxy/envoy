#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/common/passthrough.h"
#include "source/extensions/transport_sockets/internal_upstream/config.h"
#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace IstioInternalUpstream {

// Filter state key under which the upstream connection ID is published. Internal
// listeners reached through this socket can the ID.
constexpr absl::string_view UpstreamConnectionIdFilterStateKey = "io.istio.upstream_connection_id";

// A mutable filter-state object holding the upstream connection ID. The object
// is created at socket construction time (before the ID is known) and shared,
// by pointer, with the set of objects handed to the core InternalSocket base.
// The ID is populated once the transport socket callbacks are set.
class UpstreamConnectionIdObject : public StreamInfo::FilterState::Object {
public:
  void set(uint64_t value) { value_ = value; }
  uint64_t value() const { return value_; }

  std::optional<std::string> serializeAsString() const override { return std::to_string(value_); }

private:
  uint64_t value_{0};
};

// A transport socket that extends the core `internal_upstream` socket, reusing
// its metadata/filter-state passthrough logic, and additionally publishes the
// upstream connection ID as a filter-state object so the internal listener
// reached through this socket can access it.
class IstioInternalSocket : public InternalUpstream::InternalSocket,
                            public Logger::Loggable<Logger::Id::connection> {
public:
  IstioInternalSocket(Network::TransportSocketPtr inner_socket,
                      std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                      const StreamInfo::FilterState::Objects& filter_state_objects);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;

private:
  IstioInternalSocket(Network::TransportSocketPtr inner_socket,
                      std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                      const StreamInfo::FilterState::Objects& filter_state_objects,
                      std::shared_ptr<UpstreamConnectionIdObject> connection_id);

  static StreamInfo::FilterState::Objects
  withConnectionId(const StreamInfo::FilterState::Objects& objects,
                   const std::shared_ptr<UpstreamConnectionIdObject>& connection_id);

  const std::shared_ptr<UpstreamConnectionIdObject> connection_id_;
};

// Creates IstioInternalSocket instances, extracting per-host metadata and
// forwarding downstream shared filter-state objects, mirroring the core
// InternalSocketFactory behavior.
class IstioInternalSocketFactory : public TransportSockets::PassthroughFactory {
public:
  IstioInternalSocketFactory(
      Server::Configuration::TransportSocketFactoryContext& context,
      const envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport&
          config_proto,
      Network::UpstreamTransportSocketFactoryPtr&& inner_factory);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;

private:
  const InternalUpstream::Config config_;
};

} // namespace IstioInternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
