#include "common/network/utility.h"

#include "extensions/filters/listener/original_dst/original_dst.h"

#include "test/extensions/filters/listener/original_dst/original_dst_fuzz_test.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

class FakeConnectionSocket : public Network::MockConnectionSocket {
  const Network::Address::InstanceConstSharedPtr& local_address_;

public:
  ~FakeConnectionSocket() override = default;

  FakeConnectionSocket(const Network::Address::InstanceConstSharedPtr& local_address)
      : local_address_(local_address) {}

  const Network::Address::InstanceConstSharedPtr& localAddress() const override {
    return local_address_;
  }

  Network::Address::Type addressType() const override { return local_address_->type(); }

  absl::optional<Network::Address::IpVersion> ipVersion() const override {
    if (local_address_->type() != Network::Address::Type::Ip) {
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
};

DEFINE_PROTO_FUZZER(
    const envoy::extensions::filters::listener::original_dst::v3::OriginalDstTestCase& input) {

  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  Network::Address::InstanceConstSharedPtr address = nullptr;

  try {
    address = Network::Utility::resolveUrl(input.address());
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  FakeConnectionSocket socket(address);
  ON_CALL(callbacks, socket()).WillByDefault(testing::ReturnRef(socket));

  auto filter = std::make_unique<OriginalDstFilter>();
  filter->onAccept(callbacks);
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
