#pragma once

#include <chrono>
#include <memory>
#include <tuple>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/socket.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

/**
 * A socket passed to a connection. For server connections this represents the accepted socket, and
 * for client connections this represents the socket being connected to a remote address.
 *
 * TODO(jrajahalme): Hide internals (e.g., fd) from listener filters by providing callbacks filters
 * may need (set/getsockopt(), peek(), recv(), etc.)
 */
class ConnectionSocket : public virtual Socket, public virtual ScopeTrackedObject {
public:
  /**
   * Set detected transport protocol (e.g. RAW_BUFFER, TLS).
   */
  virtual void setDetectedTransportProtocol(absl::string_view protocol) PURE;

  /**
   * @return detected transport protocol (e.g. RAW_BUFFER, TLS), if any.
   */
  virtual absl::string_view detectedTransportProtocol() const PURE;

  /**
   * Set requested application protocol(s) (e.g. ALPN in TLS).
   */
  virtual void
  setRequestedApplicationProtocols(const std::vector<absl::string_view>& protocol) PURE;

  /**
   * @return requested application protocol(s) (e.g. ALPN in TLS), if any.
   */
  virtual const std::vector<std::string>& requestedApplicationProtocols() const PURE;

  /**
   * Set requested server name (e.g. SNI in TLS).
   */
  virtual void setRequestedServerName(absl::string_view server_name) PURE;

  /**
   * @return requested server name (e.g. SNI in TLS), if any.
   */
  virtual absl::string_view requestedServerName() const PURE;

  /**
   *  @return absl::optional<std::chrono::milliseconds> An optional of the most recent round-trip
   *  time of the connection. If the platform does not support this, then an empty optional is
   *  returned.
   */
  virtual absl::optional<std::chrono::milliseconds> lastRoundTripTime() PURE;
};

using ConnectionSocketPtr = std::unique_ptr<ConnectionSocket>;

} // namespace Network
} // namespace Envoy
