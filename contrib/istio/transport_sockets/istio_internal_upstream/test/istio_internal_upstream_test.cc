#include "envoy/config/core/v3/base.pb.h"

#include "source/extensions/io_socket/user_space/io_handle.h"

#include "contrib/istio/transport_sockets/istio_internal_upstream/source/istio_internal_upstream.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace IstioInternalUpstream {
namespace {

using IoSocket::UserSpace::IoHandle;
using IoSocket::UserSpace::PassthroughState;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class TestObject : public StreamInfo::FilterState::Object {};

class MockUserSpaceIoHandle : public Network::MockIoHandle, public IoHandle {
public:
  MOCK_METHOD(void, setEof, ());
  MOCK_METHOD(bool, hasReceivedEof, (), (const));
  MOCK_METHOD(void, onPeerDestroy, ());
  MOCK_METHOD(void, setNewDataAvailable, ());
  MOCK_METHOD(Buffer::Instance*, getReceiveBuffer, ());
  MOCK_METHOD(bool, canReceiveData, (), (const));
  MOCK_METHOD(bool, isWriteUnblocked, (), (const));
  MOCK_METHOD(void, onPeerBufferLowWatermark, ());
  MOCK_METHOD(bool, isReadable, (), (const));
  MOCK_METHOD(std::shared_ptr<PassthroughState>, passthroughState, ());
};

class MockPassthroughState : public PassthroughState {
public:
  MOCK_METHOD(void, initialize,
              (std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
               const StreamInfo::FilterState::Objects& filter_state_objects));
  MOCK_METHOD(void, mergeInto,
              (envoy::config::core::v3::Metadata & metadata, StreamInfo::FilterState& filter_state));
};

class IstioInternalSocketTest : public testing::Test {
public:
  void initialize(Network::IoHandle& io_handle) {
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle));
    socket_ = std::make_unique<IstioInternalSocket>(
        std::move(inner_socket), std::make_unique<envoy::config::core::v3::Metadata>(),
        filter_state_objects_);
    EXPECT_CALL(*inner_socket_, setTransportSocketCallbacks(_));
    socket_->setTransportSocketCallbacks(transport_callbacks_);
  }

  StreamInfo::FilterState::Objects filter_state_objects_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  std::unique_ptr<IstioInternalSocket> socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
};

TEST_F(IstioInternalSocketTest, NativeSocketNoPassthrough) {
  NiceMock<Network::MockIoHandle> io_handle;
  initialize(io_handle);
}

TEST_F(IstioInternalSocketTest, PublishesUpstreamConnectionId) {
  constexpr uint64_t connection_id = 12345;
  ON_CALL(transport_callbacks_.connection_, id()).WillByDefault(Return(connection_id));

  auto downstream_object = std::make_shared<TestObject>();
  filter_state_objects_.push_back(
      {downstream_object, StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection,
       "test.object"});

  auto state = std::make_shared<NiceMock<MockPassthroughState>>();
  NiceMock<MockUserSpaceIoHandle> io_handle;
  EXPECT_CALL(io_handle, passthroughState()).WillRepeatedly(Return(state));
  EXPECT_CALL(*state, initialize(_, _))
      .WillOnce(Invoke([&](std::unique_ptr<envoy::config::core::v3::Metadata>,
                           const StreamInfo::FilterState::Objects& objects) {
        ASSERT_EQ(2, objects.size());
        EXPECT_EQ("test.object", objects[0].name_);
        EXPECT_EQ(UpstreamConnectionIdFilterStateKey, objects[1].name_);
        EXPECT_EQ(std::to_string(connection_id), objects[1].data_->serializeAsString().value());
      }));
  initialize(io_handle);
}

} // namespace
} // namespace IstioInternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
