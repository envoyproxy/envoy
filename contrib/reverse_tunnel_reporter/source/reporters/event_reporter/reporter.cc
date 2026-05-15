#include "contrib/reverse_tunnel_reporter/source/reporters/event_reporter/reporter.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

EventReporter::EventReporter(Server::Configuration::ServerFactoryContext& context,
                             const ConfigProto& config,
                             std::vector<ReverseTunnelReporterClientPtr>&& clients)
    : context_{context}, clients_{std::move(clients)},
      stats_(generateStats(
          PROTOBUF_GET_STRING_OR_DEFAULT(config, stat_prefix, "reverse_tunnel_reporter"),
          context.scope())) {
  ENVOY_LOG(info, "Constructed with {} clients", clients_.size());
}

void EventReporter::onServerInitialized() {
  ENVOY_LOG(info, "Initialized");
  for (auto& client : clients_) {
    client->onServerInitialized(this);
  }
}

void EventReporter::reportConnectionEvent(absl::string_view node_id, absl::string_view cluster_id,
                                          absl::string_view tenant_id) {
  auto ptr = std::make_shared<ReverseTunnelEvent::Connected>(
      ReverseTunnelEvent::Connected{std::string(node_id), std::string(cluster_id),
                                    std::string(tenant_id), Envoy::SystemTime::clock::now()});

  context_.mainThreadDispatcher().post(
      [this, ptr = std::move(ptr)]() mutable { this->addConnection(std::move(ptr)); });
}

void EventReporter::reportDisconnectionEvent(absl::string_view node_id, absl::string_view) {
  std::string name = ReverseTunnelEvent::getName(node_id);
  auto ptr = std::make_shared<ReverseTunnelEvent::Disconnected>(name);

  context_.mainThreadDispatcher().post(
      [this, ptr = std::move(ptr)]() mutable { this->removeConnection(std::move(ptr)); });
}

// This is only served on the main thread so no locks needed.
ReverseTunnelEvent::ConnectionsList EventReporter::getAllConnections() {
  ASSERT(context_.mainThreadDispatcher().isThreadSafe());
  stats_.reverse_tunnel_full_pulls_total_.inc();

  ReverseTunnelEvent::ConnectionsList all_connections;
  all_connections.reserve(connections_.size());

  for (auto& [key, val] : connections_) {
    all_connections.push_back(val.connection);
  }
  return all_connections;
}

EventReporterStats EventReporter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  return EventReporterStats{ALL_EVENT_REPORTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                     POOL_GAUGE_PREFIX(scope, prefix))};
}

void EventReporter::notifyClients(ReverseTunnelEvent::TunnelUpdates&& updates) {
  ASSERT(!clients_.empty(), "Need at least one client. Enforced via the protos.");

  for (size_t i = 0; i < clients_.size() - 1; i++) {
    clients_[i]->receiveEvents(updates);
  }

  clients_.back()->receiveEvents(std::move(updates));
}

void EventReporter::addConnection(std::shared_ptr<ReverseTunnelEvent::Connected>&& connection) {
  ASSERT(context_.mainThreadDispatcher().isThreadSafe());

  ENVOY_LOG(info, "Accepted a new connection. Node: {}, Cluster: {}, Tenant: {}",
            connection->node_id, connection->cluster_id, connection->tenant_id);

  std::string name = ReverseTunnelEvent::getName(connection->node_id);
  auto [it, inserted] = connections_.try_emplace(std::move(name), std::move(connection), 1);

  if (inserted) {
    stats_.reverse_tunnel_unique_active_.inc();
    notifyClients(ReverseTunnelEvent::TunnelUpdates{{it->second.connection}, {}});
  } else {
    // Multiple reverse tunnels can share the same name (same node).
    // We ref-count them and only notify clients of removal when the last one disconnects.
    it->second.count++;
  }

  stats_.reverse_tunnel_established_total_.inc();
  stats_.reverse_tunnel_active_.inc();
}

void EventReporter::removeConnection(
    std::shared_ptr<ReverseTunnelEvent::Disconnected>&& disconnection) {
  ASSERT(context_.mainThreadDispatcher().isThreadSafe());

  const auto& name = disconnection->name;
  auto it = connections_.find(name);

  ENVOY_LOG(info, "Removed connection. Name: {}", name);

  if (it == connections_.end()) {
    ENVOY_LOG(warn, "Tried to remove a connection which doesnt exist");
    return;
  }

  // Only notify removal on the last ref — see addConnection for the ref-count rationale.
  if (it->second.count == 1) {
    connections_.erase(it);
    stats_.reverse_tunnel_unique_active_.dec();
    notifyClients(ReverseTunnelEvent::TunnelUpdates{{}, {disconnection}});
  } else {
    it->second.count--;
  }

  stats_.reverse_tunnel_closed_total_.inc();
  stats_.reverse_tunnel_active_.dec();
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
