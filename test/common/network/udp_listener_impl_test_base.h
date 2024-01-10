#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"

#include "test/common/network/listener_impl_test_base.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

class UdpListenerImplTestBase : public ListenerImplTestBase {
protected:
  MockIoHandle&
  useHotRestartSocket(OptRef<ParentDrainedCallbackRegistrar> parent_drained_callback_registrar) {
    auto io_handle = std::make_unique<testing::NiceMock<MockIoHandle>>();
    MockIoHandle& ret = *io_handle;
    server_socket_ = createServerSocketFromExistingHandle(std::move(io_handle),
                                                          parent_drained_callback_registrar);
    return ret;
  }

  void setup() {
    if (server_socket_ == nullptr) {
      server_socket_ = createServerSocket(true);
    }
    send_to_addr_ = Address::InstanceConstSharedPtr(getServerLoopbackAddress());
    time_system_.advanceTimeWait(std::chrono::milliseconds(100));
  }

  Address::Instance* getServerLoopbackAddress() {
    if (version_ == Address::IpVersion::v4) {
      return new Address::Ipv4Instance(
          Network::Test::getLoopbackAddressString(version_),
          server_socket_->connectionInfoProvider().localAddress()->ip()->port());
    }
    return new Address::Ipv6Instance(
        Network::Test::getLoopbackAddressString(version_),
        server_socket_->connectionInfoProvider().localAddress()->ip()->port());
  }

  SocketSharedPtr createServerSocket(bool bind) {
    // Set IP_FREEBIND to allow sendmsg to send with non-local IPv6 source address.
    return std::make_shared<UdpListenSocket>(Network::Test::getCanonicalLoopbackAddress(version_),
#ifdef IP_FREEBIND
                                             SocketOptionFactory::buildIpFreebindOptions(),
#else
                                             nullptr,
#endif
                                             bind);
  }

  SocketSharedPtr createServerSocketFromExistingHandle(
      IoHandlePtr&& io_handle,
      OptRef<ParentDrainedCallbackRegistrar> parent_drained_callback_registrar) {
    return std::make_shared<UdpListenSocket>(
        std::move(io_handle), Network::Test::getCanonicalLoopbackAddress(version_),
        SocketOptionFactory::buildIpFreebindOptions(), parent_drained_callback_registrar);
  }

  Address::InstanceConstSharedPtr getNonDefaultSourceAddress() {
    // Use a self address that is unlikely to be picked by source address discovery
    // algorithm if not specified in recvmsg/recvmmsg. Port is not taken into
    // consideration.
    Address::InstanceConstSharedPtr send_from_addr;
    if (version_ == Address::IpVersion::v4) {
      // Linux kernel regards any 127.x.x.x as local address. But Mac OS doesn't.
      send_from_addr = std::make_shared<Address::Ipv4Instance>(
          "127.0.0.1", server_socket_->connectionInfoProvider().localAddress()->ip()->port());
    } else {
      // Only use non-local v6 address if IP_FREEBIND is supported. Otherwise use
      // ::1 to avoid EINVAL error. Unfortunately this can't verify that sendmsg with
      // customized source address is doing the work because kernel also picks ::1
      // if it's not specified in cmsghdr.
      send_from_addr = std::make_shared<Address::Ipv6Instance>(
#ifdef IP_FREEBIND
          "::9",
#else
          "::1",
#endif
          server_socket_->connectionInfoProvider().localAddress()->ip()->port());
    }
    return send_from_addr;
  }

  // Validates receive data, source/destination address and received time.
  void validateRecvCallbackParams(const UdpRecvData& data, size_t num_packet_per_recv) {
    ASSERT_NE(data.addresses_.local_, nullptr);

    ASSERT_NE(data.addresses_.peer_, nullptr);
    ASSERT_NE(data.addresses_.peer_->ip(), nullptr);

    EXPECT_EQ(data.addresses_.local_->asString(), send_to_addr_->asString());

    EXPECT_EQ(data.addresses_.peer_->ip()->addressAsString(),
              client_.localAddress()->ip()->addressAsString());

    EXPECT_EQ(*data.addresses_.local_, *send_to_addr_);

    EXPECT_EQ(time_system_.monotonicTime(),
              data.receive_time_ +
                  std::chrono::milliseconds(
                      (num_packets_received_by_listener_ % num_packet_per_recv) * 100));
    // Advance time so that next onData() should have different received time.
    time_system_.advanceTimeWait(std::chrono::milliseconds(100));
    ++num_packets_received_by_listener_;
  }

  SocketSharedPtr server_socket_;
  Network::Test::UdpSyncPeer client_{GetParam()};
  Address::InstanceConstSharedPtr send_to_addr_;
  NiceMock<MockUdpListenerCallbacks> listener_callbacks_;
  NiceMock<MockListenerConfig> listener_config_;
  std::unique_ptr<UdpListenerImpl> listener_;
  size_t num_packets_received_by_listener_{0};
  Network::UdpPacketWriterPtr udp_packet_writer_;
};

} // namespace Network
} // namespace Envoy
