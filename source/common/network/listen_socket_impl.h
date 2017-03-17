#pragma once

#include "envoy/network/listen_socket.h"

#include "common/ssl/context_impl.h"

namespace Network {

class ListenSocketImpl : public ListenSocket {
public:
  ~ListenSocketImpl() { close(); }

  // Network::ListenSocket
  Address::InstancePtr localAddress() override { return local_address_; }
  int fd() override { return fd_; }

  void close() override {
    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }
  }

protected:
  void doBind();

  int fd_;
  Address::InstancePtr local_address_;
};

/**
 * Wraps a unix socket.
 */
class TcpListenSocket : public ListenSocketImpl {
public:
  TcpListenSocket(const Address::InstancePtr& address, bool bind_to_port);
  TcpListenSocket(uint32_t port, bool bind_to_port);
  TcpListenSocket(int fd, uint32_t port);
  TcpListenSocket(int fd, const Address::InstancePtr& address);
};

typedef std::unique_ptr<TcpListenSocket> TcpListenSocketPtr;

class UdsListenSocket : public ListenSocketImpl {
public:
  UdsListenSocket(const std::string& uds_path);
};

} // Network
