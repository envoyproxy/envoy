#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/platform.h"
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

  bool hasValue() const { return value_.has_value(); }
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
   * Type of sockets supported. See man 2 socket for more details
   */
  enum class Type { Stream, Datagram };

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
  virtual Socket::Type socketType() const PURE;

  /**
   * @return the type (IP or pipe) of addresses used by the socket (subset of socket domain)
   */
  virtual Address::Type addressType() const PURE;

  /**
   * @return the IP version used by the socket if address type is IP, absl::nullopt otherwise
   */
  virtual absl::optional<Address::IpVersion> ipVersion() const PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;

  /**
   * Return true if close() hasn't been called.
   */
  virtual bool isOpen() const PURE;

  /**
   * Bind a socket to this address. The socket should have been created with a call to socket()
   * @param address address to bind the socket to.
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   *   is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult bind(const Address::InstanceConstSharedPtr address) PURE;

  /**
   * Listen on bound socket.
   * @param backlog maximum number of pending connections for listener
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   *   is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult listen(int backlog) PURE;

  /**
   * Connect a socket to this address. The socket should have been created with a call to socket()
   * on this object.
   * @param address remote address to connect to.
   * @return a Api::SysCallIntResult with rc_ = 0 for success and rc_ = -1 for failure. If the call
   *   is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult connect(const Address::InstanceConstSharedPtr address) PURE;

  /**
   * Propagates option to underlying socket (@see man 2 setsockopt)
   */
  virtual Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                                socklen_t optlen) PURE;

  /**
   * Retrieves option from underlying socket (@see man 2 getsockopt)
   */
  virtual Api::SysCallIntResult getSocketOption(int level, int optname, void* optval,
                                                socklen_t* optlen) const PURE;

  /**
   * Toggle socket blocking state
   */
  virtual Api::SysCallIntResult setBlockingForTest(bool blocking) PURE;

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

class SocketInterface {
public:
  virtual ~SocketInterface() = default;

  /**
   * Low level api to create a socket in the underlying host stack. Does not create a
   * @ref Network::SocketImpl
   * @param type type of socket requested
   * @param addr_type type of address used with the socket
   * @param version IP version if address type is IP
   * @return @ref Network::IoHandlePtr that wraps the underlying socket file descriptor
   */
  virtual IoHandlePtr socket(Socket::Type type, Address::Type addr_type,
                             Address::IpVersion version) PURE;

  /**
   * Low level api to create a socket in the underlying host stack. Does not create an
   * @ref Network::SocketImpl
   * @param socket_type type of socket requested
   * @param addr address that is gleaned for address type and version if needed
   * @return @ref Network::IoHandlePtr that wraps the underlying socket file descriptor
   */
  virtual IoHandlePtr socket(Socket::Type socket_type,
                             const Address::InstanceConstSharedPtr addr) PURE;

  /**
   * Wrap socket file descriptor in IoHandle
   * @param fd socket file descriptor to be wrapped
   * @return @ref Network::IoHandlePtr that wraps the socket file descriptor
   */
  virtual IoHandlePtr socket(os_fd_t fd) PURE;

  /**
   * Returns true if the given family is supported on this machine.
   * @param domain the IP family.
   */
  virtual bool ipFamilySupported(int domain) PURE;
};

using SocketInterfacePtr = std::unique_ptr<SocketInterface>;

} // namespace Network
} // namespace Envoy