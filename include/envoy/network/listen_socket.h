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
  ~ConnectionSocket() override = default;

  /**
   * @return the remote address of the socket.
   */
  virtual const Address::InstanceConstSharedPtr& remoteAddress() const PURE;

  /**
   * @return the direct remote address of the socket. This is the address of the directly
   *         connected peer, and cannot be modified by listener filters.
   */
  virtual const Address::InstanceConstSharedPtr& directRemoteAddress() const PURE;

  /**
   * Restores the local address of the socket. On accepted sockets the local address defaults to the
   * one at which the connection was received at, which is the same as the listener's address, if
   * the listener is bound to a specific address. Call this to restore the address to a value
   * different from the one the socket was initially accepted at. This should only be called when
   * restoring the original destination address of a connection redirected by iptables REDIRECT. The
   * caller is responsible for making sure the new address is actually different.
   *
   * @param local_address the new local address.
   */
  virtual void restoreLocalAddress(const Address::InstanceConstSharedPtr& local_address) PURE;

  /**
   * Set the remote address of the socket.
   */
  virtual void setRemoteAddress(const Address::InstanceConstSharedPtr& remote_address) PURE;

  /**
   * @return true if the local address has been restored to a value that is different from the
   *         address the socket was initially accepted at.
   */
  virtual bool localAddressRestored() const PURE;

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
