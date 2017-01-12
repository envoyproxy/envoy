#pragma once

#include "envoy/network/listen_socket.h"

#include "common/ssl/context_impl.h"

namespace Network {

class ListenSocketImpl : public ListenSocket {
public:
  ListenSocketImpl() {}
  ListenSocketImpl(int fd) : fd_(fd) {}
  virtual ~ListenSocketImpl() { close(); }

  // Network::ListenSocket
  int fd() { return fd_; }

  void close() {
    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }
  }

protected:
  int fd_;
};

/**
 * Wraps a unix socket.
 */
class TcpListenSocket : public ListenSocketImpl {
public:
  TcpListenSocket(uint32_t port, bool bind_to_port);
  TcpListenSocket(int fd, uint32_t port) : ListenSocketImpl(fd), port_(port) {}

  uint32_t port() { return port_; }

  // Network::ListenSocket
  const std::string name() { return std::to_string(port_); }

private:
  uint32_t port_;
};

typedef std::unique_ptr<TcpListenSocket> TcpListenSocketPtr;

class UdsListenSocket : public ListenSocketImpl {
public:
  UdsListenSocket(const std::string& uds_path);

  // Network::ListenSocket
  const std::string name() { return "uds"; }
};

} // Network
