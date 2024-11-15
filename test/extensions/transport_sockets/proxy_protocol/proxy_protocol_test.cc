#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/proxy_protocol.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "source/extensions/transport_sockets/proxy_protocol/proxy_protocol.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNull;
using testing::ReturnRef;

using envoy::config::core::v3::ProxyProtocolConfig;
using envoy::config::core::v3::ProxyProtocolConfig_Version;
using envoy::config::core::v3::ProxyProtocolPassThroughTLVs;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {
namespace {

class ProxyProtocolTest : public testing::Test {
public:
  ProxyProtocolTest()
      : stats_(UpstreamProxyProtocolSocketFactory::generateUpstreamProxyProtocolStats(
            *stats_store_.rootScope())) {}
  void initialize(ProxyProtocolConfig& config,
                  Network::TransportSocketOptionsConstSharedPtr socket_options) {
    auto inner_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
    inner_socket_ = inner_socket.get();
    ON_CALL(transport_callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    proxy_protocol_socket_ = std::make_unique<UpstreamProxyProtocolSocket>(
        std::move(inner_socket), socket_options, config, stats_);
    proxy_protocol_socket_->setTransportSocketCallbacks(transport_callbacks_);
    proxy_protocol_socket_->onConnected();
  }

  NiceMock<Network::MockTransportSocket>* inner_socket_;
  NiceMock<Network::MockIoHandle> io_handle_;
  std::unique_ptr<UpstreamProxyProtocolSocket> proxy_protocol_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks_;
  Stats::TestUtil::TestStore stats_store_;
  UpstreamProxyProtocolStats stats_;
};

// Test injects PROXY protocol header only once
TEST_F(ProxyProtocolTest, InjectesHeaderOnlyOnce) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, nullptr);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  auto msg2 = Buffer::OwnedImpl("more data");

  {
    InSequence s;
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg2), false));
  }

  proxy_protocol_socket_->doWrite(msg, false);
  proxy_protocol_socket_->doWrite(msg2, false);
}

// Test returned bytes processed includes the PROXY protocol header
TEST_F(ProxyProtocolTest, BytesProcessedIncludesProxyProtocolHeader) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, nullptr);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
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

// Test returns KeepOpen action when write error is Again
TEST_F(ProxyProtocolTest, ReturnsKeepOpenWhenWriteErrorIsAgain) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, nullptr);

  auto msg = Buffer::OwnedImpl("some data");
  {
    InSequence s;
    EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
        .WillOnce(Invoke([&](Buffer::Instance&) -> Api::IoCallUint64Result {
          return {0, Network::IoSocketError::getIoSocketEagainError()};
        }));
    EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
        .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
          auto length = buffer.length();
          buffer.drain(length);
          return {length, Api::IoError::none()};
        }));
    EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false))
        .WillOnce(Return(Network::IoResult{Network::PostIoAction::KeepOpen, msg.length(), false}));
  }

  auto resp = proxy_protocol_socket_->doWrite(msg, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, resp.action_);
  auto resp2 = proxy_protocol_socket_->doWrite(msg, false);
  EXPECT_EQ(Network::PostIoAction::KeepOpen, resp2.action_);
}

// Test returns Close action when write error is not Again
TEST_F(ProxyProtocolTest, ReturnsCloseWhenWriteErrorIsNotAgain) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, nullptr);

  auto msg = Buffer::OwnedImpl("some data");
  {
    InSequence s;
    EXPECT_CALL(io_handle_, write(_))
        .WillOnce(Invoke([&](Buffer::Instance&) -> Api::IoCallUint64Result {
          return {0, Network::IoSocketError::create(EADDRNOTAVAIL)};
        }));
  }

  auto resp = proxy_protocol_socket_->doWrite(msg, false);
  EXPECT_EQ(Network::PostIoAction::Close, resp.action_);
}

// Test injects V1 PROXY protocol using upstream addresses when transport options are null
TEST_F(ProxyProtocolTest, V1IPV4LocalAddressWhenTransportOptionsAreNull) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, nullptr);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol using upstream addresses when header options are null
TEST_F(ProxyProtocolTest, V1IPV4LocalAddressesWhenHeaderOptionsAreNull) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://172.0.0.1:80"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("174.2.2.222", "172.0.0.1", 50000, 80,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, std::make_shared<Network::TransportSocketOptionsImpl>());

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = 43;
        buffer.drain(length);
        return {static_cast<unsigned long>(length), Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol using upstream addresses when header options are null
TEST_F(ProxyProtocolTest, V1IPV6LocalAddressesWhenHeaderOptionsAreNull) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://[a:b:c:d::]:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("a:b:c:d::", "e:b:c:f::", 50000, 8080,
                                          Network::Address::IpVersion::v6, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, std::make_shared<Network::TransportSocketOptionsImpl>());

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol for downstream IPV4 addresses
TEST_F(ProxyProtocolTest, V1IPV4DownstreamAddresses) {
  auto src_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv4Instance("202.168.0.13", 52000));
  auto dst_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv4Instance("174.2.2.222", 80));
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(
              Network::ProxyProtocolData{src_addr, dst_addr}));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://174.2.2.222:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://172.0.0.1:8080"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("202.168.0.13", "174.2.2.222", 52000, 80,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V1 PROXY protocol for downstream IPV6 addresses
TEST_F(ProxyProtocolTest, V1IPV6DownstreamAddresses) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("1::2:3", 52000));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("a:b:c:d::", 80));
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(
              Network::ProxyProtocolData{src_addr, dst_addr}));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://[a:b:c:d::]:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV1Header("1::2:3", "a:b:c:d::", 52000, 80,
                                          Network::Address::IpVersion::v6, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol using upstream addresses when transport options are null
TEST_F(ProxyProtocolTest, V2IPV4LocalCommandWhenTransportOptionsAreNull) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://1.2.3.4:773"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://0.1.1.2:513"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2LocalHeader(expected_buff);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  initialize(config, nullptr);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol using upstream addresses when header options are null
TEST_F(ProxyProtocolTest, V2IPV4LocalCommandWhenHeaderOptionsAreNull) {
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://1.2.3.4:773"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://0.1.1.2:513"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2LocalHeader(expected_buff);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  initialize(config, std::make_shared<Network::TransportSocketOptionsImpl>());

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV4 addresses
TEST_F(ProxyProtocolTest, V2IPV4DownstreamAddresses) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(
              Network::ProxyProtocolData{src_addr, dst_addr}));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://0.1.1.2:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://3.3.3.3:80"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2Header("1.2.3.4", "0.1.1.2", 773, 513,
                                          Network::Address::IpVersion::v4, expected_buff);
  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV6 addresses
TEST_F(ProxyProtocolTest, V2IPV6DownstreamAddresses) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("1:2:3::4", 8));
  auto dst_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv6Instance("1:100:200:3::", 2));
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(
              Network::ProxyProtocolData{src_addr, dst_addr}));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://[1:100:200:3::]:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080"));
  Buffer::OwnedImpl expected_buff{};
  Common::ProxyProtocol::generateV2Header("1:2:3::4", "1:100:200:3::", 8, 2,
                                          Network::Address::IpVersion::v6, expected_buff);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test onConnected calls inner onConnected
TEST_F(ProxyProtocolTest, OnConnectedCallsInnerOnConnected) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("1:2:3::4", 8));
  auto dst_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv6Instance("1:100:200:3::", 2));
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(
              Network::ProxyProtocolData{src_addr, dst_addr}));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://[1:100:200:3::]:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080"));

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  initialize(config, socket_options);

  EXPECT_CALL(*inner_socket_, onConnected());
  proxy_protocol_socket_->onConnected();
}

// Test injects V2 PROXY protocol for downstream IPV4 addresses and TLVs
TEST_F(ProxyProtocolTest, V2IPV4DownstreamAddressesAndTLVs) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  // TLV type 0x5 is PP2_TYPE_UNIQUE_ID
  Network::ProxyProtocolTLVVector tlv_vector{Network::ProxyProtocolTLV{0x5, {'a', 'b', 'c'}}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, tlv_vector};
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(proxy_proto_data));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://0.1.1.2:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://3.3.3.3:80"));
  Buffer::OwnedImpl expected_buff{};
  absl::flat_hash_set<uint8_t> pass_tlvs_set{};
  Common::ProxyProtocol::generateV2Header(proxy_proto_data, expected_buff, true, pass_tlvs_set);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  auto pass_through_tlvs = config.mutable_pass_through_tlvs();
  pass_through_tlvs->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE_ALL);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV4 addresses and TLVs with passing specific TLV.
TEST_F(ProxyProtocolTest, V2IPV4PassSpecificTLVs) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  // TLV type 0x5 is PP2_TYPE_UNIQUE_ID
  Network::ProxyProtocolTLVVector tlv_vector{Network::ProxyProtocolTLV{0x5, {'a', 'b', 'c'}}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, tlv_vector};
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(proxy_proto_data));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://0.1.1.2:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://3.3.3.3:80"));
  Buffer::OwnedImpl expected_buff{};
  absl::flat_hash_set<uint8_t> pass_tlvs_set{0x05};
  Common::ProxyProtocol::generateV2Header(proxy_proto_data, expected_buff, false, pass_tlvs_set);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  auto pass_through_tlvs = config.mutable_pass_through_tlvs();
  pass_through_tlvs->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE);
  pass_through_tlvs->add_tlv_type(0x05);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV4 addresses and TLVs with empty passing TLV set.
TEST_F(ProxyProtocolTest, V2IPV4PassEmptyTLVs) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));
  // TLV type 0x5 is PP2_TYPE_UNIQUE_ID
  Network::ProxyProtocolTLVVector tlv_vector{Network::ProxyProtocolTLV{0x5, {'a', 'b', 'c'}}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, tlv_vector};
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(proxy_proto_data));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://0.1.1.2:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://3.3.3.3:80"));
  Buffer::OwnedImpl expected_buff{};
  absl::flat_hash_set<uint8_t> pass_tlvs_set{};
  Common::ProxyProtocol::generateV2Header(proxy_proto_data, expected_buff, false, pass_tlvs_set);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  config.mutable_pass_through_tlvs()->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV4 addresses with exceeding TLV max length.
TEST_F(ProxyProtocolTest, V2IPV4TLVsExceedLengthLimit) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("1.2.3.4", 773));
  auto dst_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("0.1.1.2", 513));

  const std::string long_tlv(65536, 'a');
  Network::ProxyProtocolTLV tlv{0x5, std::vector<unsigned char>(long_tlv.begin(), long_tlv.end())};

  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, {tlv}};
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(proxy_proto_data));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://0.1.1.2:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://3.3.3.3:80"));

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  config.mutable_pass_through_tlvs()->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE_ALL);
  initialize(config, socket_options);

  auto msg = Buffer::OwnedImpl("some data");
  proxy_protocol_socket_->doWrite(msg, false);
  EXPECT_EQ(stats_store_.counter("upstream.proxyprotocol.v2_tlvs_exceed_max_length").value(), 1);
}

// Test injects V2 PROXY protocol for downstream IPV6 addresses and TLVs
TEST_F(ProxyProtocolTest, V2IPV6DownstreamAddressesAndTLVs) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("1:2:3::4", 8));
  auto dst_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv6Instance("1:100:200:3::", 2));
  // TLV type 0x5 is PP2_TYPE_UNIQUE_ID
  Network::ProxyProtocolTLVVector tlv_vector{Network::ProxyProtocolTLV{0x5, {'a', 'b', 'c'}}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, tlv_vector};
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(proxy_proto_data));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://[1:100:200:3::]:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080"));
  Buffer::OwnedImpl expected_buff{};
  absl::flat_hash_set<uint8_t> pass_through_tlvs{};
  Common::ProxyProtocol::generateV2Header(proxy_proto_data, expected_buff, true, pass_through_tlvs);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  config.mutable_pass_through_tlvs()->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE_ALL);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

// Test injects V2 PROXY protocol for downstream IPV6 addresses and TLVs without pass TLV config.
TEST_F(ProxyProtocolTest, V2IPV6DownstreamAddressesAndTLVsWithoutPassConfig) {
  auto src_addr =
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv6Instance("1:2:3::4", 8));
  auto dst_addr = Network::Address::InstanceConstSharedPtr(
      new Network::Address::Ipv6Instance("1:100:200:3::", 2));
  // TLV type 0x5 is PP2_TYPE_UNIQUE_ID
  Network::ProxyProtocolTLVVector tlv_vector{Network::ProxyProtocolTLV{0x5, {'a', 'b', 'c'}}};
  Network::ProxyProtocolData proxy_proto_data{src_addr, dst_addr, tlv_vector};
  Network::TransportSocketOptionsConstSharedPtr socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
          absl::optional<Network::ProxyProtocolData>(proxy_proto_data));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(*Network::Utility::resolveUrl("tcp://[1:100:200:3::]:50000"));
  transport_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(*Network::Utility::resolveUrl("tcp://[e:b:c:f::]:8080"));
  Buffer::OwnedImpl expected_buff{};
  absl::flat_hash_set<uint8_t> pass_through_tlvs{};
  Common::ProxyProtocol::generateV2Header(proxy_proto_data, expected_buff, false,
                                          pass_through_tlvs);

  ProxyProtocolConfig config;
  config.set_version(ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V2);
  initialize(config, socket_options);

  EXPECT_CALL(io_handle_, write(BufferStringEqual(expected_buff.toString())))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        auto length = buffer.length();
        buffer.drain(length);
        return {length, Api::IoError::none()};
      }));
  auto msg = Buffer::OwnedImpl("some data");
  EXPECT_CALL(*inner_socket_, doWrite(BufferEqual(&msg), false));

  proxy_protocol_socket_->doWrite(msg, false);
}

class ProxyProtocolSocketFactoryTest : public testing::Test {
public:
  void initialize() {
    auto inner_factory = std::make_unique<NiceMock<Network::MockTransportSocketFactory>>();
    inner_factory_ = inner_factory.get();
    factory_ = std::make_unique<UpstreamProxyProtocolSocketFactory>(
        std::move(inner_factory), ProxyProtocolConfig(), *stats_store_.rootScope());
  }

  NiceMock<Network::MockTransportSocketFactory>* inner_factory_;
  std::unique_ptr<UpstreamProxyProtocolSocketFactory> factory_;
  Stats::TestUtil::TestStore stats_store_;
};

// Test createTransportSocket returns nullptr if inner call returns nullptr
TEST_F(ProxyProtocolSocketFactoryTest, CreateSocketReturnsNullWhenInnerFactoryReturnsNull) {
  initialize();
  EXPECT_CALL(*inner_factory_, createTransportSocket(_, _)).WillOnce(ReturnNull());
  ASSERT_EQ(nullptr, factory_->createTransportSocket(nullptr, nullptr));
}

// Test implementsSecureTransport calls inner factory
TEST_F(ProxyProtocolSocketFactoryTest, ImplementsSecureTransportCallInnerFactory) {
  initialize();
  EXPECT_CALL(*inner_factory_, implementsSecureTransport()).WillOnce(Return(true));
  ASSERT_TRUE(factory_->implementsSecureTransport());
}

} // namespace
} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
