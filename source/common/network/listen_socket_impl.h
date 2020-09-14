#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/platform.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/socket.h"
#include "envoy/network/socket_interface.h"

#include "common/common/assert.h"
#include "common/network/socket_impl.h"

namespace Envoy {
namespace Network {

class ListenSocketImpl : public SocketImpl {
protected:
  ListenSocketImpl(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& local_address)
      : SocketImpl(std::move(io_handle), local_address) {}

  void setupSocket(const Network::Socket::OptionsSharedPtr& options, bool bind_to_port);
  void setListenSocketOptions(const Network::Socket::OptionsSharedPtr& options);
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
};

/**
 * Wraps a unix socket.
 */
template <Socket::Type T> struct NetworkSocketTrait {};

template <> struct NetworkSocketTrait<Socket::Type::Stream> {
  static constexpr Socket::Type type = Socket::Type::Stream;
};

template <> struct NetworkSocketTrait<Socket::Type::Datagram> {
  static constexpr Socket::Type type = Socket::Type::Datagram;
};

template <typename T> class NetworkListenSocket : public ListenSocketImpl {
public:
  NetworkListenSocket(const Address::InstanceConstSharedPtr& address,
                      const Network::Socket::OptionsSharedPtr& options, bool bind_to_port)
      : ListenSocketImpl(Network::ioHandleForAddr(T::type, address), address) {
    RELEASE_ASSERT(io_handle_->isOpen(), "");

    setPrebindSocketOptions();

    setupSocket(options, bind_to_port);
  }

  NetworkListenSocket(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& address,
                      const Network::Socket::OptionsSharedPtr& options)
      : ListenSocketImpl(std::move(io_handle), address) {
    setListenSocketOptions(options);
  }

  Socket::Type socketType() const override { return T::type; }

protected:
  void setPrebindSocketOptions();
};

using TcpListenSocket = NetworkListenSocket<NetworkSocketTrait<Socket::Type::Stream>>;
using TcpListenSocketPtr = std::unique_ptr<TcpListenSocket>;

using UdpListenSocket = NetworkListenSocket<NetworkSocketTrait<Socket::Type::Datagram>>;
using UdpListenSocketPtr = std::unique_ptr<UdpListenSocket>;

class UdsListenSocket : public ListenSocketImpl {
public:
  UdsListenSocket(const Address::InstanceConstSharedPtr& address);
  UdsListenSocket(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& address);
  Socket::Type socketType() const override { return Socket::Type::Stream; }
};

class ConnectionSocketImpl : public ConnectionSocket {
public:
  ConnectionSocketImpl(IoHandlePtr&& io_handle,
                       const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address)
      : socket_(std::move(io_handle), local_address), remote_address_(remote_address),
        direct_remote_address_(remote_address) {}

  ConnectionSocketImpl(Socket::Type type, const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address)
      : socket_(type, local_address), remote_address_(remote_address),
        direct_remote_address_(remote_address) {
    socket_.setLocalAddress(local_address);
  }

  // Network::Socket
  Socket::Type socketType() const override { return Socket::Type::Stream; }

  // Network::ConnectionSocket
  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }
  const Address::InstanceConstSharedPtr& directRemoteAddress() const override {
    return direct_remote_address_;
  }
  const Address::InstanceConstSharedPtr& localAddress() const override {
    return socket_.localAddress();
  }
  void setLocalAddress(const Address::InstanceConstSharedPtr& local_address) override {
    socket_.setLocalAddress(local_address);
  }
  IoHandle& ioHandle() override { return socket_.ioHandle(); }

  const IoHandle& ioHandle() const override { return socket_.ioHandle(); }
  Address::Type addressType() const override { return socket_.addressType(); }
  absl::optional<Address::IpVersion> ipVersion() const override { return socket_.ipVersion(); }
  void close() override { return socket_.close(); }
  bool isOpen() const override { return socket_.isOpen(); }
  Api::SysCallIntResult bind(const Address::InstanceConstSharedPtr address) override {
    return socket_.bind(address);
  }
  Api::SysCallIntResult listen(int backlog) override { return socket_.listen(backlog); }
  Api::SysCallIntResult connect(const Address::InstanceConstSharedPtr address) override {
    return socket_.connect(address);
  }
  Api::SysCallIntResult setBlockingForTest(bool blocking) override {
    return socket_.setBlockingForTest(blocking);
  }

  Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                        socklen_t optlen) override {
    return socket_.setSocketOption(level, optname, optval, optlen);
  }

  Api::SysCallIntResult getSocketOption(int level, int optname, void* optval,
                                        socklen_t* optlen) const override {
    return socket_.getSocketOption(level, optname, optval, optlen);
  }

  void addOption(const OptionConstSharedPtr& option) override { return socket_.addOption(option); }
  void addOptions(const OptionsSharedPtr& option) override { return socket_.addOptions(option); }
  const OptionsSharedPtr& options() const override { return socket_.options(); }

  void restoreLocalAddress(const Address::InstanceConstSharedPtr& local_address) override {
    socket_.setLocalAddress(local_address);
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
  SocketImpl socket_;
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
      : ConnectionSocketImpl(std::move(io_handle), local_address, remote_address) {
    ++global_accepted_socket_count_;
  }

  ~AcceptedSocketImpl() override {
    ASSERT(global_accepted_socket_count_.load() > 0);
    --global_accepted_socket_count_;
  }

  // TODO (tonya11en): Global connection count tracking is temporarily performed via a static
  // variable until the logic is moved into the overload manager.
  static uint64_t acceptedSocketCount() { return global_accepted_socket_count_.load(); }

private:
  static std::atomic<uint64_t> global_accepted_socket_count_;
};

// InternalConnectionSocketImpl used with internal listener. The owned IoHandle is not referring to
// any OS fd.
class InternalConnectionSocketImpl : public ConnectionSocketImpl {
public:
  InternalConnectionSocketImpl(IoHandlePtr&& io_handle,
                               const Address::InstanceConstSharedPtr& local_address,
                               const Address::InstanceConstSharedPtr& remote_address)
      : ConnectionSocketImpl(std::move(io_handle), local_address, remote_address) {}
  ~InternalConnectionSocketImpl() override = default;
};

// ConnectionSocket used with client connections.
class ClientSocketImpl : public ConnectionSocketImpl {
public:
  ClientSocketImpl(const Address::InstanceConstSharedPtr& remote_address,
                   const OptionsSharedPtr& options)
      : ConnectionSocketImpl(Network::ioHandleForAddr(Socket::Type::Stream, remote_address),
                             nullptr, remote_address) {
    if (options) {
      socket_.addOptions(options);
    }
  }
};

} // namespace Network
} // namespace Envoy
