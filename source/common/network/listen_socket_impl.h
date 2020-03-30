#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/platform.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

class SocketImpl : public virtual Socket {
public:
  ~SocketImpl() override { close(); }

  // Network::Socket
  const Address::InstanceConstSharedPtr& localAddress() const override { return local_address_; }
  void setLocalAddress(const Address::InstanceConstSharedPtr& local_address) override {
    local_address_ = local_address;
  }

  IoHandle& ioHandle() override { return *io_handle_; }
  const IoHandle& ioHandle() const override { return *io_handle_; }
  void close() override {
    if (io_handle_->isOpen()) {
      io_handle_->close();
    }
  }
  bool isOpen() const override { return io_handle_->isOpen(); }
  void ensureOptions() {
    if (!options_) {
      options_ = std::make_shared<std::vector<OptionConstSharedPtr>>();
    }
  }
  void addOption(const OptionConstSharedPtr& option) override {
    ensureOptions();
    options_->emplace_back(std::move(option));
  }
  void addOptions(const OptionsSharedPtr& options) override {
    ensureOptions();
    Network::Socket::appendOptions(options_, options);
  }
  const OptionsSharedPtr& options() const override { return options_; }

protected:
  SocketImpl(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& local_address)
      : io_handle_(std::move(io_handle)), local_address_(local_address) {}

  const IoHandlePtr io_handle_;
  Address::InstanceConstSharedPtr local_address_;
  OptionsSharedPtr options_;
};

class ListenSocketImpl : public SocketImpl {
protected:
  ListenSocketImpl(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& local_address)
      : SocketImpl(std::move(io_handle), local_address) {}

  void setupSocket(const Network::Socket::OptionsSharedPtr& options, bool bind_to_port);
  void doBind();
  void setListenSocketOptions(const Network::Socket::OptionsSharedPtr& options);
};

/**
 * Wraps a unix socket.
 */
template <Address::SocketType T> struct NetworkSocketTrait {};

template <> struct NetworkSocketTrait<Address::SocketType::Stream> {
  static constexpr Address::SocketType type = Address::SocketType::Stream;
};

template <> struct NetworkSocketTrait<Address::SocketType::Datagram> {
  static constexpr Address::SocketType type = Address::SocketType::Datagram;
};

template <typename T> class NetworkListenSocket : public ListenSocketImpl {
public:
  NetworkListenSocket(const Address::InstanceConstSharedPtr& address,
                      const Network::Socket::OptionsSharedPtr& options, bool bind_to_port)
      : ListenSocketImpl(address->socket(T::type), address) {
    RELEASE_ASSERT(SOCKET_VALID(io_handle_->fd()), "");

    setPrebindSocketOptions();

    setupSocket(options, bind_to_port);
  }

  NetworkListenSocket(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& address,
                      const Network::Socket::OptionsSharedPtr& options)
      : ListenSocketImpl(std::move(io_handle), address) {
    setListenSocketOptions(options);
  }

  Address::SocketType socketType() const override { return T::type; }

protected:
  void setPrebindSocketOptions();
};

using TcpListenSocket = NetworkListenSocket<NetworkSocketTrait<Address::SocketType::Stream>>;
using TcpListenSocketPtr = std::unique_ptr<TcpListenSocket>;

using UdpListenSocket = NetworkListenSocket<NetworkSocketTrait<Address::SocketType::Datagram>>;
using UdpListenSocketPtr = std::unique_ptr<UdpListenSocket>;

class UdsListenSocket : public ListenSocketImpl {
public:
  UdsListenSocket(const Address::InstanceConstSharedPtr& address);
  UdsListenSocket(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& address);
  Address::SocketType socketType() const override { return Address::SocketType::Stream; }
};

class ConnectionSocketImpl : public SocketImpl, public ConnectionSocket {
public:
  ConnectionSocketImpl(IoHandlePtr&& io_handle,
                       const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address)
      : SocketImpl(std::move(io_handle), local_address), remote_address_(remote_address),
        direct_remote_address_(remote_address) {}

  // Network::Socket
  Address::SocketType socketType() const override { return Address::SocketType::Stream; }

  // Network::ConnectionSocket
  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }
  const Address::InstanceConstSharedPtr& directRemoteAddress() const override {
    return direct_remote_address_;
  }
  void restoreLocalAddress(const Address::InstanceConstSharedPtr& local_address) override {
    setLocalAddress(local_address);
    local_address_restored_ = true;
  }
  void setRemoteAddress(const Address::InstanceConstSharedPtr& remote_address) override {
    remote_address_ = remote_address;
  }
  bool localAddressRestored() const override { return local_address_restored_; }

  void setDetectedTransportProtocol(absl::string_view protocol) override {
    transport_protocol_ = std::string(protocol);
  }
  absl::string_view detectedTransportProtocol() const override { return transport_protocol_; }

  void setRequestedApplicationProtocols(const std::vector<absl::string_view>& protocols) override {
    application_protocols_.clear();
    for (const auto& protocol : protocols) {
      application_protocols_.emplace_back(protocol);
    }
  }
  const std::vector<std::string>& requestedApplicationProtocols() const override {
    return application_protocols_;
  }

  void setRequestedServerName(absl::string_view server_name) override {
    server_name_ = std::string(server_name);
  }
  absl::string_view requestedServerName() const override { return server_name_; }

protected:
  Address::InstanceConstSharedPtr remote_address_;
  const Address::InstanceConstSharedPtr direct_remote_address_;
  bool local_address_restored_{false};
  std::string transport_protocol_;
  std::vector<std::string> application_protocols_;
  std::string server_name_;
};

// ConnectionSocket used with server connections.
class AcceptedSocketImpl : public ConnectionSocketImpl {
public:
  AcceptedSocketImpl(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& local_address,
                     const Address::InstanceConstSharedPtr& remote_address)
      : ConnectionSocketImpl(std::move(io_handle), local_address, remote_address) {}
};

// ConnectionSocket used with client connections.
class ClientSocketImpl : public ConnectionSocketImpl {
public:
  ClientSocketImpl(const Address::InstanceConstSharedPtr& remote_address,
                   const OptionsSharedPtr& options)
      : ConnectionSocketImpl(remote_address->socket(Address::SocketType::Stream), nullptr,
                             remote_address) {
    if (options) {
      addOptions(options);
    }
  }
};

} // namespace Network
} // namespace Envoy
