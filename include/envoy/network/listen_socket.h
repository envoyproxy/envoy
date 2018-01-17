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
 * A socket passed to a connection.
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
   * @return true if the local address has been reset.
   */
  virtual bool localAddressReset() const PURE;

  /**
   * @return fd the socket's file descriptor.
   */
  virtual int fd() const PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;
};

typedef std::unique_ptr<ConnectionSocket> ConnectionSocketPtr;

/**
 * An abstract accepted socket.
 *
 * TODO(jrajahalme): Hide internals (e.g., fd) from the filters by providing callbacks filters
 * may need (set/getsockopt(), peek(), recv(), etc.)
 */
class AcceptedSocket : virtual public ConnectionSocket {
public:
  virtual ~AcceptedSocket() {}

  /**
   * Reset the destination address of the socket to a different address than the one
   * the socket was accepted at.
   */
  virtual void resetLocalAddress(const Address::InstanceConstSharedPtr& local_address) PURE;

  /**
   * Reset the source address of the socket to a different address than the one
   * the socket was accepted at.
   */
  virtual void resetRemoteAddress(const Address::InstanceConstSharedPtr& remote_address) PURE;
};

typedef std::unique_ptr<AcceptedSocket> AcceptedSocketPtr;

/**
 * An abstract client socket used with ClientConnections.
 */
class ClientSocket : virtual public ConnectionSocket {
public:
  virtual ~ClientSocket() {}

  /**
   * Set the local address of the socket.
   */
  virtual void setLocalAddress(const Address::InstanceConstSharedPtr& local_address) PURE;
};

} // namespace Network
} // namespace Envoy
