#pragma once

#include <memory>
#include <optional>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/scope.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Handshake request parsed by a responder handshake. Every view is valid only during
// ResponderHandshakeCallbacks::onTunnelRequest().
struct TunnelRequest {
  absl::string_view node_id;
  absl::string_view cluster_id;
  absl::string_view tenant_id;
  // Attributes the JWT check and the %REQ(...)% identifier validation read. A non-HTTP handshake
  // supplies the equivalent key/values.
  const Http::RequestHeaderMap& headers;
  // Raw initiation time in epoch millis, absent when not advertised. The filter parses it.
  std::optional<absl::string_view> initiation_time_ms;
  absl::string_view initiator_worker_id;
  absl::string_view initiator_connection_id;
};

// Decision on a tunnel request. Both views point at constants. The reply does not carry
// ``details``.
struct TunnelDecision {
  bool accepted{false};
  Http::Code code{Http::Code::Forbidden};
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

  // Runs the policy checks on a framed request and, when they pass, prepares the tunnel: the
  // socket is duplicated and reads are disabled. Called once per connection, and increments its
  // own stats. On accept the handshake sends its acceptance reply and then calls
  // onAcceptanceResponseEncoded(). On reject it sends ``code`` and ``body`` and closes the
  // connection with FlushWrite.
  virtual TunnelDecision onTunnelRequest(const TunnelRequest& request) = 0;

  // Stats sink for a framing rejection; the handshake sends the matching reply itself.
  virtual void onFramingRejection(HandshakeFramingRejection rejection) = 0;

  // A rejection reply is about to be sent and the connection closed, so the handshake deadline no
  // longer applies.
  virtual void onTerminalRejection() = 0;

  // The acceptance reply has been encoded. ``response_wire_bytes`` is its exact size on the wire;
  // the filter registers the tunnel once that many bytes are sent.
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
