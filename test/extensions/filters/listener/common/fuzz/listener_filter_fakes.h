#include "common/api/os_sys_calls_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/utility.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class FakeConnectionSocket : public Network::MockConnectionSocket {
public:
  FakeConnectionSocket()
      : io_handle_(std::make_unique<Network::IoSocketHandleImpl>(42))
      , local_address_(nullptr), remote_address_(nullptr) {}

  ~FakeConnectionSocket() override { io_handle_->close(); }

  Network::IoHandle& ioHandle() override { return *io_handle_; }

  void setLocalAddress(const Network::Address::InstanceConstSharedPtr& local_address) override {
    local_address_ = local_address;
    if (local_address_ != nullptr) {
      addr_type_ = local_address_->type();
    }
  }

  void setRemoteAddress(const Network::Address::InstanceConstSharedPtr& remote_address) override {
    remote_address_ = remote_address;
  }

  const Network::Address::InstanceConstSharedPtr& localAddress() const override { return local_address_; }

  const Network::Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }

  Network::Address::Type addressType() const override { return addr_type_; }

  absl::optional<Network::Address::IpVersion> ipVersion() const override {
    if (addr_type_ != Network::Address::Type::Ip) {
      return absl::nullopt;
    }

    return local_address_->ip()->version();
  }

  Api::SysCallIntResult getSocketOption(int level, int, void* optval, socklen_t*) const override {
    switch (level) {
    case SOL_IPV6:
      static_cast<sockaddr_storage*>(optval)->ss_family = AF_INET6;
      break;
    case SOL_IP:
      static_cast<sockaddr_storage*>(optval)->ss_family = AF_INET;
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }

    return Api::SysCallIntResult{0, 0};
  }

private:
  const Network::IoHandlePtr io_handle_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr remote_address_;
  Network::Address::Type addr_type_;
};

class FakeOsSysCalls : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(Api::SysCallSizeResult, recv, (os_fd_t, void*, size_t, int));
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
