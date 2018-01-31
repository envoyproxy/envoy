#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Network {

/**
 * An abstract listen socket.
 */
class ListenSocket {
public:
  virtual ~ListenSocket() {}

  /**
   * @return the address that the socket is listening on.
   */
  virtual Address::InstanceConstSharedPtr localAddress() const PURE;

  /**
   * @return fd the listen socket's file descriptor.
   */
  virtual int fd() PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;
};

typedef std::unique_ptr<ListenSocket> ListenSocketPtr;
typedef std::shared_ptr<ListenSocket> ListenSocketSharedPtr;

/**
 * A socket passed to a connection. For server connections this represents the accepted socket, and
 * for client connections this represents the socket being connected to a remote address.
 *
 * TODO(jrajahalme): Hide internals (e.g., fd) from listener filters by providing callbacks filters
 * may need (set/getsockopt(), peek(), recv(), etc.)
 */
class ConnectionSocket {
public:
  virtual ~ConnectionSocket() {}

  /**
   * @return the local address of the socket.
   */
  virtual const Address::InstanceConstSharedPtr& localAddress() const PURE;

  /**
   * @return the remote address of the socket.
   */
  virtual const Address::InstanceConstSharedPtr& remoteAddress() const PURE;

  /**
   * Set the local address of the socket. On accepted sockets the local address defaults to the
   * one at which the connection was received at, which is the same as the listener's address, if
   * the listener is bound to a specific address.
   *
   * @param local_address the new local address.
   * @param restored a flag marking the local address as being restored to a value that is
   *        different from the one the socket was initially accepted at. This should only be set
   *        to 'true' when restoring the original destination address of a connection redirected
   *        by iptables REDIRECT. The caller is responsible for making sure the new address is
   *        actually different when passing restored as 'true'.
   */
  virtual void setLocalAddress(const Address::InstanceConstSharedPtr& local_address,
                               bool restored = false) PURE;

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
   * @return fd the socket's file descriptor.
   */
  virtual int fd() const PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;

  /**
   * Visitor class for setting socket options.
   */
  class Options {
  public:
    virtual ~Options() {}

    /**
     * @param socket the socket on which to apply options.
     * @return true if succeeded, false otherwise.
     */
    virtual bool setOptions(ConnectionSocket& socket) const PURE;

    /**
     * @return bits that can be used to separate connections based on the options. Should return
     *         zero if connections with different options can be pooled together. This is limited
     *         to 32 bits to allow these bits to be efficiently combined into a larger hash key
     *         used in connection pool lookups.
     */
    virtual uint32_t hashKey() const PURE;
  };
  typedef std::shared_ptr<Options> OptionsSharedPtr;

  /**
   * Set the socket options for later retrieval with options().
   */
  virtual void setOptions(const OptionsSharedPtr&) PURE;

  /**
   * @return the socket options stored earlier with setOptions(), if any.
   */
  virtual const ConnectionSocket::OptionsSharedPtr& options() const PURE;
};

typedef std::unique_ptr<ConnectionSocket> ConnectionSocketPtr;

} // namespace Network
} // namespace Envoy
