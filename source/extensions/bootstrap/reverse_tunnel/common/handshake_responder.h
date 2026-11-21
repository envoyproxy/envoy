#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/http/codes.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Optional initiator metadata parsed from an accepted handshake. The identifiers are not carried
// here: the filter registers the ones its last passing evaluatePolicy() approved.
struct AcceptedTunnelInfo {
  int64_t initiation_time_ms{0};
  std::string initiator_worker_id;
  std::string initiator_connection_id;
};

// Policy decision on the handshake's identity, authentication, and authorization checks.
struct HandshakePolicyResult {
  bool accepted{false};
  Http::Code code{Http::Code::Forbidden};
  // Both views point at constants. The reply does not carry ``details``.
  absl::string_view body;
  absl::string_view details;
};

// Framing rejections whose stats are owned by the filter; the reply is sent by the handshake.
enum class HandshakeFramingRejection { ParseError, BodyRejected };

/**
 * Callbacks from a responder handshake into the owning filter. All methods run synchronously on
 * the worker that owns the handshake connection, from within the handshake's onData.
 */
class ResponderHandshakeCallbacks {
public:
  virtual ~ResponderHandshakeCallbacks() = default;

  // Connection carrying the handshake.
  virtual Network::Connection& connection() = 0;

  // Scope the handshake allocates its codec resources in.
  virtual Stats::Scope& statsScope() = 0;

  // Overload manager backing the connection, consulted when codec resources are created.
  virtual Server::OverloadManager& overloadManager() = 0;

  // Policy gate run once framing validation passes: empty tenant, tenant-isolation delimiters,
  // required cluster name, JWT authentication, and identifier validation. The string views alias
  // the request headers and are only valid for the duration of the call. Increments its own stats.
  // A passing result records the identifiers for prepareAcceptedConnection().
  virtual HandshakePolicyResult evaluatePolicy(const Http::RequestHeaderMap& headers,
                                               absl::string_view node_id,
                                               absl::string_view cluster_id,
                                               absl::string_view tenant_id) = 0;

  // Stats sink for a framing rejection; the handshake sends the matching reply itself.
  virtual void onFramingRejection(HandshakeFramingRejection rejection) = 0;

  // A rejection reply is about to be sent and the connection closed, so the handshake deadline no
  // longer applies.
  virtual void onTerminalRejection() = 0;

  // Phase one of acceptance: duplicate the socket and hold registration state for the identifiers
  // the preceding evaluatePolicy() approved. Valid once, after a passing evaluatePolicy(). Returns
  // false when the tunnel cannot be registered; the handshake answers 503 and the filter owns the
  // stat.
  virtual bool prepareAcceptedConnection(const AcceptedTunnelInfo& info) = 0;

  // The acceptance response has been encoded; the filter orders phase two behind it reaching the
  // wire.
  virtual void onAcceptanceResponseEncoded(uint64_t response_wire_bytes) = 0;
};

/**
 * Server-side handshake on the connection the filter owns. One instance per connection; onData is
 * called with every received byte until the connection is closed.
 */
class ResponderHandshake {
public:
  virtual ~ResponderHandshake() = default;

  virtual Network::FilterStatus onData(Buffer::Instance& data) = 0;
};

using ResponderHandshakePtr = std::unique_ptr<ResponderHandshake>;

/**
 * Creates responder handshakes. Implemented per handshake protocol; a protocol-agnostic seam for
 * the filter, which always talks to a ResponderHandshake.
 */
class ResponderHandshakeConfigurator {
public:
  virtual ~ResponderHandshakeConfigurator() = default;

  virtual ResponderHandshakePtr createHandshake(ResponderHandshakeCallbacks& callbacks) const = 0;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
