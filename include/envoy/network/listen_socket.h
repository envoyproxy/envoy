#pragma once

#include <memory>
#include <tuple>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/io_handle.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

// SocketOptionName is an optional value that captures the setsockopt(2)
// arguments. The idea here is that if a socket option is not supported
// on a platform, we can make this the empty value, which allows us to
// avoid #ifdef proliferation.
struct SocketOptionName {
  SocketOptionName() = default;
  SocketOptionName(const SocketOptionName&) = default;
  SocketOptionName(int level, int option, const std::string& name)
      : value_(std::make_tuple(level, option, name)) {}

  int level() const { return std::get<0>(value_.value()); }
  int option() const { return std::get<1>(value_.value()); }
  const std::string& name() const { return std::get<2>(value_.value()); }

  bool has_value() const { return value_.has_value(); }
  bool operator==(const SocketOptionName& rhs) const { return value_ == rhs.value_; }

private:
  absl::optional<std::tuple<int, int, std::string>> value_;
};

// ENVOY_MAKE_SOCKET_OPTION_NAME is a helper macro to generate a
// SocketOptionName with a descriptive string name.
#define ENVOY_MAKE_SOCKET_OPTION_NAME(level, option)                                               \
  Network::SocketOptionName(level, option, #level "/" #option)

/**
 * Base class for Sockets
 */
class Socket {
public:
  virtual ~Socket() = default;

  /**
   * @return the local address of the socket.
   */
  virtual const Address::InstanceConstSharedPtr& localAddress() const PURE;

  /**
   * Set the local address of the socket. On accepted sockets the local address defaults to the
   * one at which the connection was received at, which is the same as the listener's address, if
   * the listener is bound to a specific address.
   *
   * @param local_address the new local address.
   */
  virtual void setLocalAddress(const Address::InstanceConstSharedPtr& local_address) PURE;

  /**
   * @return IoHandle for the underlying connection
   */
  virtual IoHandle& ioHandle() PURE;

  /**
   * @return const IoHandle for the underlying connection
   */
  virtual const IoHandle& ioHandle() const PURE;

  /**
   * @return the type (stream or datagram) of the socket.
   */
  virtual Address::SocketType socketType() const PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;

  /**
   * Return true if close() hasn't been called.
   */
  virtual bool isOpen() const PURE;

  /**
   * Visitor class for setting socket options.
   */
  class Option {
  public:
    virtual ~Option() = default;

    /**
     * @param socket the socket on which to apply options.
     * @param state the current state of the socket. Significant for options that can only be
     *        set for some particular state of the socket.
     * @return true if succeeded, false otherwise.
     */
    virtual bool setOption(Socket& socket,
                           envoy::config::core::v3::SocketOption::SocketState state) const PURE;

    /**
     * @param vector of bytes to which the option should append hash key data that will be used
     *        to separate connections based on the option. Any data already in the key vector must
     *        not be modified.
     */
    virtual void hashKey(std::vector<uint8_t>& key) const PURE;

    /**
     * Contains details about what this option applies to a socket.
     */
    struct Details {
      SocketOptionName name_;
      std::string value_; ///< Binary string representation of an option's value.

      bool operator==(const Details& other) const {
        return name_ == other.name_ && value_ == other.value_;
      }
    };

    /**
     * @param socket The socket for which we want to know the options that would be applied.
     * @param state The state at which we would apply the options.
     * @return What we would apply to the socket at the provided state. Empty if we'd apply nothing.
     */
    virtual absl::optional<Details>
    getOptionDetails(const Socket& socket,
                     envoy::config::core::v3::SocketOption::SocketState state) const PURE;
  };

  using OptionConstSharedPtr = std::shared_ptr<const Option>;
  using Options = std::vector<OptionConstSharedPtr>;
  using OptionsSharedPtr = std::shared_ptr<Options>;

  static OptionsSharedPtr& appendOptions(OptionsSharedPtr& to, const OptionsSharedPtr& from) {
    to->insert(to->end(), from->begin(), from->end());
    return to;
  }

  static bool applyOptions(const OptionsSharedPtr& options, Socket& socket,
                           envoy::config::core::v3::SocketOption::SocketState state) {
    if (options == nullptr) {
      return true;
    }
    for (const auto& option : *options) {
      if (!option->setOption(socket, state)) {
        return false;
      }
    }
    return true;
  }

  /**
   * Add a socket option visitor for later retrieval with options().
   */
  virtual void addOption(const OptionConstSharedPtr&) PURE;

  /**
   * Add socket option visitors for later retrieval with options().
   */
  virtual void addOptions(const OptionsSharedPtr&) PURE;

  /**
   * @return the socket options stored earlier with addOption() and addOptions() calls, if any.
   */
  virtual const OptionsSharedPtr& options() const PURE;
};

using SocketPtr = std::unique_ptr<Socket>;
using SocketSharedPtr = std::shared_ptr<Socket>;
using SocketOptRef = absl::optional<std::reference_wrapper<Socket>>;

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
};

using ConnectionSocketPtr = std::unique_ptr<ConnectionSocket>;

/**
 * Thrown when there is a runtime error binding a socket.
 */
class SocketBindException : public EnvoyException {
public:
  SocketBindException(const std::string& what, int error_number)
      : EnvoyException(what), error_number_(error_number) {}

  // This can't be called errno because otherwise the standard errno macro expansion replaces it.
  int errorNumber() const { return error_number_; }

private:
  const int error_number_;
};

} // namespace Network
} // namespace Envoy
