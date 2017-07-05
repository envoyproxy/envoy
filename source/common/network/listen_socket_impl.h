#pragma once

#include <unistd.h>

#include <memory>
#include <string>

#include "envoy/network/listen_socket.h"

#include "common/ssl/context_impl.h"

namespace Envoy {
namespace Network {

class ListenSocketImpl : public ListenSocket {
public:
  ~ListenSocketImpl() { close(); }

  // Network::ListenSocket
  Address::InstanceConstSharedPtr localAddress() const override { return local_address_; }
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
  Address::InstanceConstSharedPtr local_address_;
};

/**
 * Wraps a unix socket.
 */
class TcpListenSocket : public ListenSocketImpl {
public:
  TcpListenSocket(Address::InstanceConstSharedPtr address, bool bind_to_port);
  TcpListenSocket(int fd, Address::InstanceConstSharedPtr address);
};

typedef std::unique_ptr<TcpListenSocket> TcpListenSocketPtr;

class UdsListenSocket : public ListenSocketImpl {
public:
  UdsListenSocket(const std::string& uds_path);
};

} // namespace Network
} // namespace Envoy
