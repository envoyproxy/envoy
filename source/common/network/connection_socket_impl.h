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
