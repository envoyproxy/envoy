#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.h"

#include "common/network/utility.h"

#include "extensions/filters/listener/original_dst/original_dst.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {
namespace {

using testing::Return;
class OriginalDstTest : public testing::Test {
public:
  OriginalDstTest() {}
  std::unique_ptr<OriginalDstFilter> makeDefaultFilter() {
    Config default_config;
    return std::make_unique<OriginalDstFilter>(default_config);
  }

  std::unique_ptr<OriginalDstFilter> makeNoopFilter() {
    envoy::extensions::filters::listener::original_dst::v3::OriginalDst message;
    message.set_address_recovery_method(
        envoy::extensions::filters::listener::original_dst::v3::OriginalDst::Method::
            OriginalDst_Method_CurrentLocalAddress);
    Config no_op_method(message, envoy::config::core::v3::OUTBOUND);
    return std::make_unique<OriginalDstFilter>(no_op_method);
  }

protected:
  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
};

TEST_F(OriginalDstTest, DefaultMethod) {
  auto filter = makeDefaultFilter();
  EXPECT_FALSE(callbacks_.socket_.addressProvider().localAddressRestored());

  EXPECT_CALL(callbacks_.socket_, getSocketOption(_, _, _, _))
      .WillOnce(Return(Api::SysCallIntResult{0, -1}));
  // There is attempt to restore the original address.
  EXPECT_THROW_WITH_MESSAGE(filter->onAccept(callbacks_), EnvoyException,
                            "Unexpected sockaddr family: 0");
}

TEST_F(OriginalDstTest, AddressIsNotRestoredIfOriginalAddressIsNotObtained) {
  auto filter = makeDefaultFilter();
  EXPECT_FALSE(callbacks_.socket_.addressProvider().localAddressRestored());
  EXPECT_CALL(callbacks_.socket_, getSocketOption(_, _, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, -1}));
  filter->onAccept(callbacks_);
  EXPECT_FALSE(callbacks_.socket_.addressProvider().localAddressRestored());
}

TEST_F(OriginalDstTest, NoopMethodRecoverTheAddress) {
  auto filter = makeNoopFilter();
  auto local_address = callbacks_.socket_.addressProvider().localAddress();
  EXPECT_FALSE(callbacks_.socket_.addressProvider().localAddressRestored());
  filter->onAccept(callbacks_);
  EXPECT_TRUE(callbacks_.socket_.addressProvider().localAddressRestored());
  auto restored_address = callbacks_.socket_.addressProvider().localAddress();
  ASSERT_EQ(*local_address, *restored_address);
}

} // namespace
} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
