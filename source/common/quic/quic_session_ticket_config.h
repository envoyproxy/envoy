#pragma once

namespace Envoy {
namespace Quic {

// Snapshot of a QUIC listener's session-ticket-related configuration, taken at
// handshaker construction. Separated from QuicServerTransportSocketFactory so
// the handshaker can depend on the struct without pulling in the full factory
// header (and the cycle that would create).
struct QuicSessionTicketConfig {
  // True when session ticket encryption keys are explicitly configured via
  // session_ticket_keys or session_ticket_keys_sds_secret_config. Without
  // keys, the server cannot encrypt or decrypt session tickets using Envoy's
  // ticket key callback.
  bool has_keys{false};
  // True when disable_stateless_session_resumption is set in
  // DownstreamTlsContext. When enabled, the server will not issue session
  // tickets and clients must perform full handshakes on every connection.
  bool disable_stateless_resumption{false};
  // True when the configured TLS handshaker implementation handles session
  // resumption itself. When set, Envoy must not process tickets with its
  // own ticket key callback.
  bool handles_session_resumption{false};
};

} // namespace Quic
} // namespace Envoy
