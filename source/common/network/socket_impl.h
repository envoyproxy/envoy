#pragma once

#include "envoy/network/socket.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"

namespace Envoy {
namespace Network {

class SocketAddressSetterImpl : public SocketAddressSetter {
public:
  SocketAddressSetterImpl(const Address::InstanceConstSharedPtr& local_address,
                          const Address::InstanceConstSharedPtr& remote_address)
      : local_address_(local_address), remote_address_(remote_address),
        direct_remote_address_(remote_address) {}

  void setDirectRemoteAddressForTest(const Address::InstanceConstSharedPtr& direct_remote_address) {
    direct_remote_address_ = direct_remote_address;
  }

  void dumpState(std::ostream& os, int indent_level) const override {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "SocketAddressSetterImpl " << this
       << DUMP_NULLABLE_MEMBER(remote_address_, remote_address_->asStringView())
       << DUMP_NULLABLE_MEMBER(direct_remote_address_, direct_remote_address_->asStringView())
       << DUMP_NULLABLE_MEMBER(local_address_, local_address_->asStringView())
       << DUMP_MEMBER(server_name_) << "\n";
  }

  // SocketAddressSetter
  const Address::InstanceConstSharedPtr& localAddress() const override { return local_address_; }
  void setLocalAddress(const Address::InstanceConstSharedPtr& local_address) override {
    local_address_ = local_address;
  }
  void restoreLocalAddress(const Address::InstanceConstSharedPtr& local_address) override {
    setLocalAddress(local_address);
    local_address_restored_ = true;
  }
  bool localAddressRestored() const override { return local_address_restored_; }
  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }
  void setRemoteAddress(const Address::InstanceConstSharedPtr& remote_address) override {
    remote_address_ = remote_address;
  }
  const Address::InstanceConstSharedPtr& directRemoteAddress() const override {
    return direct_remote_address_;
  }
  absl::string_view requestedServerName() const override { return server_name_; }
  void setRequestedServerName(const absl::string_view requested_server_name) override {
    server_name_ = std::string(requested_server_name);
  }
  absl::optional<uint64_t> connectionID() const override { return connection_id_; }
  void setConnectionID(uint64_t id) override { connection_id_ = id; }
  Ssl::ConnectionInfoConstSharedPtr sslConnection() const override { return ssl_info_; }
  void setSslConnection(const Ssl::ConnectionInfoConstSharedPtr& ssl_connection_info) override {
    ssl_info_ = ssl_connection_info;
  }

private:
  Address::InstanceConstSharedPtr local_address_;
  bool local_address_restored_{false};
  Address::InstanceConstSharedPtr remote_address_;
  Address::InstanceConstSharedPtr direct_remote_address_;
  std::string server_name_;
  absl::optional<uint64_t> connection_id_;
  Ssl::ConnectionInfoConstSharedPtr ssl_info_;
};

class SocketImpl : public virtual Socket {
public:
  SocketImpl(Socket::Type socket_type, const Address::InstanceConstSharedPtr& address_for_io_handle,
             const Address::InstanceConstSharedPtr& remote_address);

  // Network::Socket
  SocketAddressSetter& addressProvider() override { return *address_provider_; }
  const SocketAddressProvider& addressProvider() const override { return *address_provider_; }
  SocketAddressProviderSharedPtr addressProviderSharedPtr() const override {
    return address_provider_;
  }
  SocketPtr duplicate() override {
    // Implementing the functionality here for all sockets is tricky because it leads
    // into object slicing issues.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
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

  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Api::SysCallIntResult connect(const Address::InstanceConstSharedPtr addr) override;
  Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                        socklen_t optlen) override;
  Api::SysCallIntResult getSocketOption(int level, int optname, void* optval,
                                        socklen_t* optlen) const override;
  Api::SysCallIntResult ioctl(unsigned long control_code, void* in_buffer,
                              unsigned long in_buffer_len, void* out_buffer,
                              unsigned long out_buffer_len, unsigned long* bytes_returned) override;

  Api::SysCallIntResult setBlockingForTest(bool blocking) override;

  const OptionsSharedPtr& options() const override { return options_; }
  Socket::Type socketType() const override { return sock_type_; }
  Address::Type addressType() const override { return addr_type_; }
  absl::optional<Address::IpVersion> ipVersion() const override;

protected:
  SocketImpl(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& local_address,
             const Address::InstanceConstSharedPtr& remote_address);

  const IoHandlePtr io_handle_;
  const std::shared_ptr<SocketAddressSetterImpl> address_provider_;
  OptionsSharedPtr options_;
  Socket::Type sock_type_;
  Address::Type addr_type_;
};

} // namespace Network
} // namespace Envoy
