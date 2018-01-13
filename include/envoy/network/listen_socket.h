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
 * An abstract accepted socket.
 */
class AcceptedSocket {
public:
  virtual ~AcceptedSocket() {}

  /**
   * @return the address that the socket was received at, or an original destination address if
   * applicable.
   */
  virtual Address::InstanceConstSharedPtr localAddress() const PURE;

  /**
   * Reset the original destination address of the socket to a different address than the one
   * the socket was accepted at.
   */
  virtual void resetLocalAddress(Address::InstanceConstSharedPtr& local_address) PURE;

  /**
   * @return true if the local address has been reset.
   */
  virtual bool localAddressReset() const PURE;

  /**
   * @return the address that the socket was received from, or an original source address if
   * applicable.
   */
  virtual Address::InstanceConstSharedPtr remoteAddress() const PURE;

  /**
   * Set the original source address of the socket
   */
  virtual void resetRemoteAddress(Address::InstanceConstSharedPtr& remote_address) PURE;

  /**
   * @return fd the accepted socket's file descriptor.
   */
  virtual int fd() const PURE;

  /**
   * Transfer ownership of the file descriptor to the caller, so that the underlying socket will
   * not be closed on delete.
   */
  virtual int takeFd() PURE;

  /**
   * Clear 'reset' state so that the socket can be used again with a new listener.
   */
  virtual void clearReset() PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;
};

typedef std::unique_ptr<AcceptedSocket> AcceptedSocketPtr;
typedef std::shared_ptr<AcceptedSocket> AcceptedSocketSharedPtr;

} // namespace Network
} // namespace Envoy
