#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"
#include "common/network/transport_socket_options_impl.h"

#include "extensions/transport_sockets/proxy_protocol/proxy_protocol.h"
#include "extensions/common/proxy_protocol/proxy_protocol_header.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

using envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocol_Version;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {
namespace {

constexpr uint64_t MaxSlices = 16;

class ProxyProtocolTest : public testing::Test {
public:
  ProxyProtocolTest() {}

  void initialize(ProxyProtocol_Version version,
                  Network::TransportSocketOptionsSharedPtr socket_options) {
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    proxy_protocol_socket_ =
        std::make_unique<ProxyProtocolSocket>(std::move(inner_socket), socket_options, version);
    proxy_protocol_socket_->setTransportSocketCallbacks(transport_callbacks_);
  }

  NiceMock<Network::MockTransportSocket>* inner_socket_;
  NiceMock<Network::MockIoHandle> io_handle_;
  std::unique_ptr<ProxyProtocolSocket> proxy_protocol_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
};

// Test injects PROXY protocol header only once
TEST_F(ProxyProtocolTest, InjectesHeaderOnlyOnce) {
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://174.2.2.222:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://172.0.0.1:80");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80, Network::Address::IpVersion::v4, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V1, nullptr);

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  auto msg2 = Buffer::OwnedImpl("more data");
  {
    InSequence s;
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg2), false)).Times(1);
  }

  proxy_protocol_socket_->doWrite(msg, false);
  proxy_protocol_socket_->doWrite(msg2, false);
}

// Test returned bytes processed includes the PROXY protocol header
TEST_F(ProxyProtocolTest, BytesProcessedIncludesProxyProtocolHeader) {
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://174.2.2.222:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://172.0.0.1:80");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80, Network::Address::IpVersion::v4, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V1, nullptr);

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  auto msg2 = Buffer::OwnedImpl("more data");
  {
    InSequence s;
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false))
        .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, msg.length(), false}));
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg2), false))
        .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, msg2.length(), false}));
  }


  auto resp = proxy_protocol_socket_->doWrite(msg, false);
  EXPECT_EQ(expected_buff.length() + msg.length(), resp.bytes_processed_);
  auto resp2 = proxy_protocol_socket_->doWrite(msg2, false);
  EXPECT_EQ(msg2.length(), resp2.bytes_processed_);
}

// Test injects V1 PROXY protocol using upstream addresses when transport options are null
TEST_F(ProxyProtocolTest, V1IPV4LocalAddressWhenTransportOptionsAreNull) {
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://174.2.2.222:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://172.0.0.1:80");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80, Network::Address::IpVersion::v4, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V1, nullptr);

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol using upstream addresses when transport options are null
TEST_F(ProxyProtocolTest, V1IPV4LocalAddressesWhenDownstreamAddressesAreNull) {
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://174.2.2.222:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://172.0.0.1:80");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80, Network::Address::IpVersion::v4, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V1,
             std::make_shared<Network::TransportSocketOptionsImpl>());

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), 1))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(43, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol using upstream addresses when transport options are null
TEST_F(ProxyProtocolTest, V1IPV6LocalAddressesWhenDownstreamAddressesAreNull) {
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://[a:b:c:d::]:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("a:b:c:d::", "e:b:c:f::", 50000, 8080, Network::Address::IpVersion::v6, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V1,
             std::make_shared<Network::TransportSocketOptionsImpl>());

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol for downstream IPV4 addresses
TEST_F(ProxyProtocolTest, V1IPV4DownstreamAddresses) {
  Network::TransportSocketOptionsSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{},
          Network::TransportSocketOptions::DownstreamAddresses{
              "202.168.0.13",
              "174.2.2.222",
              52000,
              80,
              Network::Address::IpVersion::v4,
          });
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://174.2.2.222:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://172.0.0.1:8080");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("202.168.0.13", "174.2.2.222", 52000, 80, Network::Address::IpVersion::v4, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V1, socket_options);

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol for downstream IPV6 addresses
TEST_F(ProxyProtocolTest, V1IPV6DownstreamAddresses) {
  Network::TransportSocketOptionsSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{},
          Network::TransportSocketOptions::DownstreamAddresses{
              "1::2:3",
              "a:b:c:d::",
              52000,
              80,
              Network::Address::IpVersion::v6,
          });
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://[a:b:c:d::]:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("1::2:3", "a:b:c:d::", 52000, 80, Network::Address::IpVersion::v6, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V1, socket_options);

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol using upstream addresses when transport options are null
TEST_F(ProxyProtocolTest, V2IPV4LocalCommandWhenTransportOptionsAreNull) {
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://1.2.3.4:773");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://0.1.1.2:513");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2LocalHeader(expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V2, nullptr);


  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol using upstream addresses when downstream addresses are null
TEST_F(ProxyProtocolTest, V2IPV4LocalCommandWhenDownstreamAddressesAreNull) {
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://1.2.3.4:773");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://0.1.1.2:513");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2LocalHeader(expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V2,
             std::make_shared<Network::TransportSocketOptionsImpl>());

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV4 addresses
TEST_F(ProxyProtocolTest, V2IPV4DownstreamAddresses) {
  Network::TransportSocketOptionsSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{},
          Network::TransportSocketOptions::DownstreamAddresses{
              "1.2.3.4",
              "0.1.1.2",
              773,
              513,
              Network::Address::IpVersion::v4,
          });
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://0.1.1.2:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://3.3.3.3:80");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2Header("1.2.3.4", "0.1.1.2", 773, 513, Network::Address::IpVersion::v4, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V2, socket_options);

  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV6 addresses
TEST_F(ProxyProtocolTest, V2IPV6DownstreamAddresses) {
  Network::TransportSocketOptionsSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{},
          Network::TransportSocketOptions::DownstreamAddresses{
              "1:2:3::4",
              "1:100:200:3::",
              8,
              2,
              Network::Address::IpVersion::v6,
          });
  transport_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://[1:100:200:3::]:50000");
  transport_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080");
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2Header("1:2:3::4", "1:100:200:3::", 8, 2, Network::Address::IpVersion::v6, expected_buff);
  auto expected_slices = expected_buff.getRawSlices(MaxSlices);
  initialize(ProxyProtocol_Version::ProxyProtocol_Version_V2, socket_options);


  EXPECT_CALL(io_handle_, writev(RawSliceVectorEqual(expected_slices), expected_slices.size()))
      .WillOnce(Return(testing::ByMove(
          Api::IoCallUint64Result(expected_buff.length(), Api::IoErrorPtr(nullptr, [](Api::IoError*) {})))));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false)).Times(1);

  proxy_protocol_socket_->doWrite(msg, false);
}

} // namespace
} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
