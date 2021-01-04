#pragma once

#include "envoy/network/socket.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

class SocketAddressProviderImpl : public SocketAddressProvider {
public:
  SocketAddressProviderImpl(const Address::InstanceConstSharedPtr& local_address,
                            const Address::InstanceConstSharedPtr& remote_address)
      : local_address_(local_address), remote_address_(remote_address),
        direct_remote_address_(remote_address) {}

  void setDirectRemoteAddressForTest(const Address::InstanceConstSharedPtr& direct_remote_address) {
    direct_remote_address_ = direct_remote_address;
  }

  // SocketAddressProvider
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

private:
  Address::InstanceConstSharedPtr local_address_;
  bool local_address_restored_{false};
  Address::InstanceConstSharedPtr remote_address_;
  Address::InstanceConstSharedPtr direct_remote_address_;
};

class SocketImpl : public virtual Socket {
public:
  SocketImpl(Socket::Type socket_type, const Address::InstanceConstSharedPtr& address_for_io_handle,
             const Address::InstanceConstSharedPtr& remote_address);

  // Network::Socket
  SocketAddressProvider& addressProvider() override { return *address_provider_; }
  const SocketAddressProviderGetters& addressProvider() const override {
    return *address_provider_;
  }
  SocketAddressProviderGettersSharedPtr addressProviderSharedPtr() const override {
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
  Api::SysCallIntResult setBlockingForTest(bool blocking) override;

  const OptionsSharedPtr& options() const override { return options_; }
  Socket::Type socketType() const override { return sock_type_; }
  Address::Type addressType() const override { return addr_type_; }
  absl::optional<Address::IpVersion> ipVersion() const override;

protected:
  SocketImpl(IoHandlePtr&& io_handle, const Address::InstanceConstSharedPtr& local_address,
             const Address::InstanceConstSharedPtr& remote_address);

  const IoHandlePtr io_handle_;
  const SocketAddressProviderSharedPtr address_provider_;
  OptionsSharedPtr options_;
  Socket::Type sock_type_;
  Address::Type addr_type_;
};

} // namespace Network
} // namespace Envoy