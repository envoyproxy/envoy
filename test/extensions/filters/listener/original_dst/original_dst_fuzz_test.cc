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

DEFINE_PROTO_FUZZER(
    const envoy::extensions::filters::listener::original_dst::v3::OriginalDstTestCase& input) {

  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnectionSocket> socket_;

  try {
    auto address_ = Network::Utility::resolveUrl(input.address());
    ON_CALL(socket_, localAddress()).WillByDefault(testing::ReturnRef(address_));
    ON_CALL(callbacks_, socket()).WillByDefault(testing::ReturnRef(socket_));

    if (address_ != nullptr) {
      ON_CALL(socket_, addressType()).WillByDefault(testing::Return(address_->type()));
      if (socket_.addressType() == Network::Address::Type::Ip) {
        ON_CALL(socket_, ipVersion()).WillByDefault(testing::Return(address_->ip()->version()));
      }
    }

  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  auto filter = std::make_unique<OriginalDstFilter>();

  // Set address family of mock socket so that it routes correctly thru addressFromSockAddr
  ON_CALL(socket_, getSocketOption(_, _, _, _))
      .WillByDefault(testing::WithArgs<0, 2>(Invoke([](int level, void* optval) {
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
      })));

  filter->onAccept(callbacks_);
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
