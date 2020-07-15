#include "common/network/utility.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class FakeConnectionSocket : public MockConnectionSocket {
public:
  ~FakeConnectionSocket() override = default;

  FakeConnectionSocket() : local_address_(nullptr), remote_address_(nullptr) {}

  FakeConnectionSocket(const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address)
      : local_address_(local_address), remote_address_(remote_address) {}

  void setLocalAddress(const Address::InstanceConstSharedPtr& local_address) override {
    local_address_ = local_address;
  }

  void setRemoteAddress(const Address::InstanceConstSharedPtr& remote_address) override {
    remote_address_ = remote_address;
  }

  const Address::InstanceConstSharedPtr& localAddress() const override { return local_address_; }

  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }

  Address::Type addressType() const override { return local_address_->type(); }

  absl::optional<Address::IpVersion> ipVersion() const override {
    if (local_address_->type() != Address::Type::Ip) {
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

  Address::InstanceConstSharedPtr local_address_;
  Address::InstanceConstSharedPtr remote_address_;
};

} // namespace Network
} // namespace Envoy
