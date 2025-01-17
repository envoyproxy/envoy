#include "source/common/network/filter_state_dst_address.h"
#include "source/common/network/listener_filter_buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/listener/original_dst/original_dst.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {
namespace {

using testing::Return;
using testing::ReturnRef;

class OriginalDstTest : public testing::Test {
public:
  OriginalDstTest() : filter_(envoy::config::core::v3::TrafficDirection::OUTBOUND) {
    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
  }

  void expectInternalAddress() {
    EXPECT_CALL(socket_, addressType())
        .WillRepeatedly(Return(Network::Address::Type::EnvoyInternal));
    EXPECT_CALL(cb_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
    EXPECT_CALL(cb_, filterState()).Times(testing::AtLeast(1));
  }

  OriginalDstFilter filter_;
  Network::MockListenerFilterCallbacks cb_;
  Network::MockConnectionSocket socket_;
  envoy::config::core::v3::Metadata metadata_;
};

TEST_F(OriginalDstTest, Pipe) {
  EXPECT_EQ(0, filter_.maxReadBytes());
  EXPECT_CALL(socket_, addressType()).WillRepeatedly(Return(Network::Address::Type::Pipe));
  filter_.onAccept(cb_);
  EXPECT_FALSE(socket_.connectionInfoProvider().localAddressRestored());

  NiceMock<Network::MockIoHandle> io_handle;
  NiceMock<Event::MockDispatcher> dispatcher;
  Network::ListenerFilterBufferImpl buffer(
      io_handle, dispatcher, [](bool) {}, [](Network::ListenerFilterBuffer&) {}, false, 1);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_.onData(buffer));
}

TEST_F(OriginalDstTest, InternalNone) {
  expectInternalAddress();
  filter_.onAccept(cb_);
  EXPECT_FALSE(socket_.connectionInfoProvider().localAddressRestored());
}

TEST_F(OriginalDstTest, InternalDynamicMetadata) {
  expectInternalAddress();
  TestUtility::loadFromYaml(R"EOF(
    filter_metadata:
      envoy.filters.listener.original_dst:
        local: 127.0.0.1:8080
  )EOF",
                            metadata_);
  filter_.onAccept(cb_);
  EXPECT_TRUE(socket_.connectionInfoProvider().localAddressRestored());
  EXPECT_EQ("127.0.0.1:8080", socket_.connectionInfoProvider().localAddress()->asString());
}

TEST_F(OriginalDstTest, InternalDynamicMetadataFailure) {
  expectInternalAddress();
  TestUtility::loadFromYaml(R"EOF(
    filter_metadata:
      envoy.filters.listener.original_dst:
        local: aaabb
  )EOF",
                            metadata_);
  filter_.onAccept(cb_);
  EXPECT_FALSE(socket_.connectionInfoProvider().localAddressRestored());
}

TEST_F(OriginalDstTest, InternalFilterState) {
  expectInternalAddress();
  const auto local = Network::Utility::parseInternetAddressNoThrow("10.20.30.40", 456, false);
  const auto remote = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8000, false);
  cb_.filter_state_.setData("envoy.filters.listener.original_dst.local_ip",
                            std::make_shared<Network::AddressObject>(local),
                            StreamInfo::FilterState::StateType::Mutable,
                            StreamInfo::FilterState::LifeSpan::Connection);
  cb_.filter_state_.setData("envoy.filters.listener.original_dst.remote_ip",
                            std::make_shared<Network::AddressObject>(remote),
                            StreamInfo::FilterState::StateType::Mutable,
                            StreamInfo::FilterState::LifeSpan::Connection);
  filter_.onAccept(cb_);
  EXPECT_TRUE(socket_.connectionInfoProvider().localAddressRestored());
  EXPECT_EQ(local->asString(), socket_.connectionInfoProvider().localAddress()->asString());
  EXPECT_EQ(remote->asString(), socket_.connectionInfoProvider().remoteAddress()->asString());
}

} // namespace
} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
