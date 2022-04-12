#include "source/common/network/address_impl.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/extensions/io_socket/user_space/io_handle.h"
#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace InternalUpstream {

namespace {

class UserSpaceIoHandle : public Network::MockIoHandle, IoSocket::UserSpace::IoHandle {
public:
  MOCK_METHOD(void, setWriteEnd, ());
  MOCK_METHOD(bool, isPeerShutDownWrite, (), (const));
  MOCK_METHOD(void, onPeerDestroy, ());
  MOCK_METHOD(void, setNewDataAvailable, ());
  MOCK_METHOD(Buffer::Instance*, getWriteBuffer, ());
  MOCK_METHOD(bool, isWritable, (), (const));
  MOCK_METHOD(bool, isPeerWritable, (), (const));
  MOCK_METHOD(void, onPeerBufferLowWatermark, ());
  MOCK_METHOD(bool, isReadable, (), (const));
  MOCK_METHOD(IoSocket::UserSpace::PassthroughStateSharedPtr, passthroughState, ());
};

class InternalSocketTest : public testing::Test {
public:
  InternalSocketTest()
      : metadata_(std::make_unique<envoy::config::core::v3::Metadata>()),
        filter_state_objects_(std::make_unique<IoSocket::UserSpace::FilterStateObjects>()) {}

  void initialize(Network::IoHandle& io_handle) {
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(testing::ReturnRef(io_handle));
    socket_ = std::make_unique<InternalSocket>(std::move(inner_socket), std::move(metadata_),
                                               std::move(filter_state_objects_));
    EXPECT_CALL(*inner_socket_, setTransportSocketCallbacks(_));
    socket_->setTransportSocketCallbacks(transport_callbacks_);
  }

  std::unique_ptr<envoy::config::core::v3::Metadata> metadata_;
  std::unique_ptr<IoSocket::UserSpace::FilterStateObjects> filter_state_objects_;
  NiceMock<Network::MockTransportSocket>* inner_socket_;
  std::unique_ptr<InternalSocket> socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
};

TEST_F(InternalSocketTest, NativeSocket) {
  NiceMock<Network::MockIoHandle> io_handle;
  initialize(io_handle);
}

TEST_F(InternalSocketTest, PassthroughStateInjected) {
  auto object = std::make_shared<Network::ProxyProtocolFilterState>(
      Network::ProxyProtocolData{Network::Address::InstanceConstSharedPtr(
                                     new Network::Address::Ipv4Instance("202.168.0.13", 52000)),
                                 Network::Address::InstanceConstSharedPtr(
                                     new Network::Address::Ipv4Instance("174.2.2.222", 80))});
  filter_state_objects_->emplace_back(Network::ProxyProtocolFilterState::key(), object);
  ProtobufWkt::Struct& map = (*metadata_->mutable_filter_metadata())["envoy.test"];
  ProtobufWkt::Value val;
  val.set_string_value("val");
  (*map.mutable_fields())["key"] = val;

  NiceMock<UserSpaceIoHandle> io_handle;
  initialize(io_handle);
}

} // namespace
} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
