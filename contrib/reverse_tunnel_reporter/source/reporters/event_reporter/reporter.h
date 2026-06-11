#pragma once

#include <vector>

#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"

#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters/event_reporter.pb.h"
#include "contrib/reverse_tunnel_reporter/source/reverse_tunnel_event_types.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

#define ALL_EVENT_REPORTER_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(reverse_tunnel_established_total)                                                        \
  COUNTER(reverse_tunnel_closed_total)                                                             \
  COUNTER(reverse_tunnel_full_pulls_total)                                                         \
  GAUGE(reverse_tunnel_active, Accumulate)                                                         \
  GAUGE(reverse_tunnel_unique_active, Accumulate)

struct EventReporterStats {
  ALL_EVENT_REPORTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

struct ConnectionEntry {
  std::shared_ptr<ReverseTunnelEvent::Connected> connection;
  std::size_t count;
};

/// Aggregates reverse-tunnel connection/disconnection events, de-duplicates by
/// name, maintains stats, and fans out batched diffs as shared ptrs to registered clients.
class EventReporter : public ReverseTunnelReporterWithState,
                      public Logger::Loggable<Logger::Id::connection> {
public:
  using ConfigProto =
      envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::EventReporterConfig;

  EventReporter(Server::Configuration::ServerFactoryContext& context, const ConfigProto& config,
                std::vector<ReverseTunnelReporterClientPtr>&& clients);

  void onServerInitialized() override;
  void reportConnectionEvent(absl::string_view node_id, absl::string_view cluster_id,
                             absl::string_view tenant_id) override;
  void reportDisconnectionEvent(absl::string_view node_id, absl::string_view cluster_id) override;
  ReverseTunnelEvent::ConnectionsList getAllConnections() override;

private:
  static EventReporterStats generateStats(const std::string& prefix, Stats::Scope& scope);
  void notifyClients(ReverseTunnelEvent::TunnelUpdates&& updates);
  void addConnection(std::shared_ptr<ReverseTunnelEvent::Connected>&& connection);
  void removeConnection(std::shared_ptr<ReverseTunnelEvent::Disconnected>&& disconnection);

  Server::Configuration::ServerFactoryContext& context_;
  std::vector<ReverseTunnelReporterClientPtr> clients_;
  EventReporterStats stats_;
  // Keyed by getName(node_id).
  absl::flat_hash_map<std::string, ConnectionEntry> connections_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
