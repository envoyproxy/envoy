#pragma once

#include <chrono>
#include <memory>
#include <tuple>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
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
class ConnectionSocket : public virtual Socket {
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
   * @param ja3Hash Connection ja3 fingerprint hash of the downstream connection.
   */
  virtual void setJA3Hash(absl::string_view ja3_hash) PURE;

  /**
   * @return Connection ja3 fingerprint hash of the downstream connection, if any.
   */
  virtual absl::string_view ja3Hash() const PURE;

  /**
   *  @return absl::optional<std::chrono::milliseconds> An optional of the most recent round-trip
   *  time of the connection. If the platform does not support this, then an empty optional is
   *  returned.
   */
  virtual absl::optional<std::chrono::milliseconds> lastRoundTripTime() PURE;

  /**
   * @return the current congestion window in bytes, or unset if not available or not
   * congestion-controlled.
   * @note some congestion controller's cwnd is measured in number of packets, in that case the
   * return value is cwnd(in packets) times the connection's MSS.
   */
  virtual absl::optional<uint64_t> congestionWindowInBytes() const PURE;

  /**
   * Dump debug state of the object in question to the provided ostream.
   *
   * This is called on Envoy fatal errors, so should do minimal memory allocation.
   *
   * @param os the ostream to output to.
   * @param indent_level how far to indent, for pretty-printed classes and subclasses.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;
};

using ConnectionSocketPtr = std::unique_ptr<ConnectionSocket>;

} // namespace Network
} // namespace Envoy
