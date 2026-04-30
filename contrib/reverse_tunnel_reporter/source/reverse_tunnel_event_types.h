#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/reverse_tunnel_reporter.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/fmt.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// The namespace holding the structs for the reverse tunnel events
namespace ReverseTunnelEvent {
// getName builds the canonical connection name used as the de-duplication key in the reporter.
// Assumption: node_id is unique.
// When tenant isolation is enabled we get the node_id here as
// ReverseConnectionUtility::buildTenantScopedIdentifier(node_id) from the reverse_tunnel code which
// invokes this.
inline std::string getName(absl::string_view node_id) { return std::string(node_id); }

struct Connected {
  std::string node_id;
  std::string cluster_id;
  std::string tenant_id;
  Envoy::SystemTime created_at;
};

struct Disconnected {
  std::string name;

  explicit Disconnected(absl::string_view n) : name(n) {}
};

// Most of the events are just single connections and disconnections.
// The clients may need to buffer them up -> some extra stack space for the same.
using ConnectionsList = absl::InlinedVector<std::shared_ptr<const Connected>, 4>;
using DisconnectionsList = absl::InlinedVector<std::shared_ptr<const Disconnected>, 4>;

struct TunnelUpdates {
  ConnectionsList connections;
  DisconnectionsList disconnections;

  std::size_t size() const { return connections.size() + disconnections.size(); }

  void operator+=(TunnelUpdates&& events) {
    connections.insert(connections.end(), std::make_move_iterator(events.connections.begin()),
                       std::make_move_iterator(events.connections.end()));

    disconnections.insert(disconnections.end(),
                          std::make_move_iterator(events.disconnections.begin()),
                          std::make_move_iterator(events.disconnections.end()));

    events.connections.clear();
    events.disconnections.clear();
  }

  void clear() {
    connections.clear();
    disconnections.clear();
  }
};
} // namespace ReverseTunnelEvent

// ReverseTunnelReporterWithState will own the clients and expose an api for them to get the full
// state. ReverseTunnelReporterWithState allows multiple clients to share data -> clients can focus
// on sending the data alone.
class ReverseTunnelReporterWithState : public ReverseTunnelReporter {
public:
  virtual ~ReverseTunnelReporterWithState() = default;

  virtual ReverseTunnelEvent::ConnectionsList getAllConnections() PURE;
};

using ReverseTunnelReporterWithStatePtr = std::unique_ptr<ReverseTunnelReporterWithState>;

// ReverseTunnelReporterClient gets the ptr to the reporter for polling all the active connections.
// ReverseTunnelReporterClient also has receiveEvents to get the diff events from the reporter.
class ReverseTunnelReporterClient {
public:
  virtual ~ReverseTunnelReporterClient() = default;

  virtual void onServerInitialized(ReverseTunnelReporterWithState* reporter) PURE;

  virtual void receiveEvents(ReverseTunnelEvent::TunnelUpdates events) PURE;
};

using ReverseTunnelReporterClientPtr = std::unique_ptr<ReverseTunnelReporterClient>;

class ReverseTunnelReporterClientFactory : public Config::TypedFactory {
public:
  virtual ReverseTunnelReporterClientPtr
  createClient(Server::Configuration::ServerFactoryContext& context,
               const Protobuf::Message& config) PURE;

  std::string category() const override {
    return "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.clients";
  }
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
