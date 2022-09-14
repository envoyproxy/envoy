#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/platform.h"
#include "envoy/network/connection.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/socket.h"
#include "envoy/network/socket_interface.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Network {

class ListenSocketImpl : public SocketImpl {
protected:
  ListenSocketImpl(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& local_address)
      : SocketImpl(std::move(io_handle), local_address, nullptr) {}

  SocketPtr duplicate() override {
    // Using `new` to access a non-public constructor.
    return absl::WrapUnique(
        new ListenSocketImpl(io_handle_ == nullptr ? nullptr : io_handle_->duplicate(),
                             connection_info_provider_->localAddress()));
  }

  void setupSocket(const Network::Socket::OptionsSharedPtr& options);
  void setListenSocketOptions(const Network::Socket::OptionsSharedPtr& options);
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;

  void close() override {
    if (io_handle_ != nullptr && io_handle_->isOpen()) {
      io_handle_->close();
    }
  }
  bool isOpen() const override { return io_handle_ != nullptr && io_handle_->isOpen(); }
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
                      const Network::Socket::OptionsSharedPtr& options, bool bind_to_port,
                      const SocketCreationOptions& creation_options = {})
      : ListenSocketImpl(bind_to_port ? Network::ioHandleForAddr(T::type, address, creation_options)
                                      : nullptr,
                         address) {
    // Prebind is applied if the socket is bind to port.
    if (bind_to_port) {
      RELEASE_ASSERT(io_handle_->isOpen(), "");
      setPrebindSocketOptions();
      setupSocket(options);
    } else {
      // If the tcp listener does not bind to port, we test that the ip family is supported.
      if (auto ip = address->ip(); ip != nullptr) {
        RELEASE_ASSERT(
            Network::SocketInterfaceSingleton::get().ipFamilySupported(ip->ipv4() ? AF_INET
                                                                                  : AF_INET6),
            fmt::format("Creating listen socket address {} but the address family is not supported",
                        address->asStringView()));
      }
    }
  }

  NetworkListenSocket(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& address,
                      const Network::Socket::OptionsSharedPtr& options)
      : ListenSocketImpl(std::move(io_handle), address) {
    setListenSocketOptions(options);
  }

  Socket::Type socketType() const override { return T::type; }

  SocketPtr duplicate() override {
    if (io_handle_ == nullptr) {
      // This is a listen socket that does not bind to port. Pass nullptr socket options.
      return std::make_unique<NetworkListenSocket<T>>(connection_info_provider_->localAddress(),
                                                      /*options=*/nullptr, /*bind_to_port*/ false);
    } else {
      return ListenSocketImpl::duplicate();
    }
  }

  // These four overrides are introduced to perform check. A null io handle is possible only if the
  // the owner socket is a listen socket that does not bind to port.
  IoHandle& ioHandle() override {
    ASSERT(io_handle_ != nullptr);
    return *io_handle_;
  }
  const IoHandle& ioHandle() const override {
    ASSERT(io_handle_ != nullptr);
    return *io_handle_;
  }
  void close() override {
    if (io_handle_ != nullptr) {
      if (io_handle_->isOpen()) {
        io_handle_->close();
      }
    }
  }
  bool isOpen() const override {
    return io_handle_ == nullptr ? false // Consider listen socket as closed if it does not bind to
                                         // port. No fd will leak.
                                 : io_handle_->isOpen();
  }

protected:
  void setPrebindSocketOptions() {
    // On Windows, SO_REUSEADDR does not restrict subsequent bind calls when there is a listener as
    // on Linux and later BSD socket stacks.
#ifndef WIN32
    int on = 1;
    auto status = setSocketOption(SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    RELEASE_ASSERT(status.return_value_ != -1, "failed to set SO_REUSEADDR socket option");
#endif
  }
};

template <>
inline void
NetworkListenSocket<NetworkSocketTrait<Socket::Type::Datagram>>::setPrebindSocketOptions() {}

template class NetworkListenSocket<NetworkSocketTrait<Socket::Type::Stream>>;
template class NetworkListenSocket<NetworkSocketTrait<Socket::Type::Datagram>>;

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

// This socket type adapts the ListenerComponentFactory.
class InternalListenSocket : public ListenSocketImpl {
public:
  InternalListenSocket(const Address::InstanceConstSharedPtr& address)
      : ListenSocketImpl(/* io_handle= */ nullptr, address) {}
  Socket::Type socketType() const override { return Socket::Type::Stream; }

  // InternalListenSocket cannot be duplicated.
  SocketPtr duplicate() override {
    return std::make_unique<InternalListenSocket>(connectionInfoProvider().localAddress());
  }

  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr) override {
    // internal listener socket does not support bind semantic.
    // TODO(lambdai) return an error.
    PANIC("not implemented");
  }

  void close() override { ASSERT(io_handle_ == nullptr); }
  bool isOpen() const override {
    ASSERT(io_handle_ == nullptr);
    return false;
  }
};

class ConnectionSocketImpl : public SocketImpl, public ConnectionSocket {
public:
  ConnectionSocketImpl(IoHandlePtr&& io_handle,
                       const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address)
      : SocketImpl(std::move(io_handle), local_address, remote_address) {}

  ConnectionSocketImpl(Socket::Type type, const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address,
                       const SocketCreationOptions& options)
      : SocketImpl(type, local_address, remote_address, options) {
    connection_info_provider_->setLocalAddress(local_address);
  }

  // Network::ConnectionSocket
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
    // Always keep the server_name_ as lower case.
    connectionInfoProvider().setRequestedServerName(absl::AsciiStrToLower(server_name));
  }
  absl::string_view requestedServerName() const override {
    return connectionInfoProvider().requestedServerName();
  }

  void setJA3Hash(absl::string_view ja3_hash) override {
    connectionInfoProvider().setJA3Hash(ja3_hash);
  }
  absl::string_view ja3Hash() const override { return connectionInfoProvider().ja3Hash(); }

  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override {
    return ioHandle().lastRoundTripTime();
  }

  absl::optional<uint64_t> congestionWindowInBytes() const override {
    return ioHandle().congestionWindowInBytes();
  }

  void dumpState(std::ostream& os, int indent_level) const override {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "ListenSocketImpl " << this << DUMP_MEMBER(transport_protocol_) << "\n";
    DUMP_DETAILS(connection_info_provider_);
  }

protected:
  std::string transport_protocol_;
  std::vector<std::string> application_protocols_;
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

// ConnectionSocket used with client connections.
class ClientSocketImpl : public ConnectionSocketImpl {
public:
  ClientSocketImpl(const Address::InstanceConstSharedPtr& remote_address,
                   const OptionsSharedPtr& options)
      : ConnectionSocketImpl(Network::ioHandleForAddr(Socket::Type::Stream, remote_address, {}),
                             nullptr, remote_address) {
    if (options) {
      addOptions(options);
    }
  }
};

} // namespace Network
} // namespace Envoy
