#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/udp_packet_writer_handler_impl.h"
#include "common/network/utility.h"

#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

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

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

class UdpListenerImplBatchWriterTest : public ListenerImplTestBase {
public:
  UdpListenerImplBatchWriterTest()
      : server_socket_(createServerSocket(true)), send_to_addr_(getServerLoopbackAddress()) {
    time_system_.advanceTimeWait(std::chrono::milliseconds(100));
  }

  void SetUp() override {
    // Set listening socket options.
    server_socket_->addOptions(SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(SocketOptionFactory::buildRxQueueOverFlowOptions());

    ON_CALL(listener_config_, udpPacketWriterFactory())
        .WillByDefault(Return(&udp_packet_writer_factory_));
    ON_CALL(udp_packet_writer_factory_, createUdpPacketWriter(_, _))
        .WillByDefault(Invoke(
            [&](Network::IoHandle& io_handle, Stats::Scope& scope) -> Network::UdpPacketWriterPtr {
              UdpPacketWriterPtr udp_packet_writer =
                  std::make_unique<Quic::UdpGsoBatchWriter>(io_handle, scope);
              return udp_packet_writer;
            }));

    listener_ =
        std::make_unique<UdpListenerImpl>(dispatcherImpl(), server_socket_, listener_callbacks_,
                                          dispatcherImpl().timeSource(), listener_config_);
  }

protected:
  Address::Instance* getServerLoopbackAddress() {
    if (version_ == Address::IpVersion::v4) {
      return new Address::Ipv4Instance(Network::Test::getLoopbackAddressString(version_),
                                       server_socket_->localAddress()->ip()->port());
    }
    return new Address::Ipv6Instance(Network::Test::getLoopbackAddressString(version_),
                                     server_socket_->localAddress()->ip()->port());
  }

  SocketSharedPtr createServerSocket(bool bind) {
    // Set IP_FREEBIND to allow sendmsg to send with non-local IPv6 source address.
    return std::make_shared<UdpListenSocket>(Network::Test::getAnyAddress(version_),
#ifdef IP_FREEBIND
                                             SocketOptionFactory::buildIpFreebindOptions(),
#else
                                             nullptr,
#endif
                                             bind);
  }

  // Validates receive data, source/destination address and received time.
  void validateRecvCallbackParams(const UdpRecvData& data) {
    ASSERT_NE(data.addresses_.local_, nullptr);

    ASSERT_NE(data.addresses_.peer_, nullptr);
    ASSERT_NE(data.addresses_.peer_->ip(), nullptr);

    EXPECT_EQ(data.addresses_.local_->asString(), send_to_addr_->asString());

    EXPECT_EQ(data.addresses_.peer_->ip()->addressAsString(),
              client_.localAddress()->ip()->addressAsString());

    EXPECT_EQ(*data.addresses_.local_, *send_to_addr_);

    size_t num_packet_per_recv = 1u;
    if (Api::OsSysCallsSingleton::get().supportsMmsg()) {
      num_packet_per_recv = 16u;
    }
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
  MockUdpListenerCallbacks listener_callbacks_;
  NiceMock<MockListenerConfig> listener_config_;
  NiceMock<MockUdpPacketWriterFactory> udp_packet_writer_factory_;
  std::unique_ptr<UdpListenerImpl> listener_;
  size_t num_packets_received_by_listener_{0};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpListenerImplBatchWriterTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

/**
 * Tests UDP Packet Writer To Send packets in Batches to a client
 *  1. Setup a udp listener and client socket
 *  2. Send different sized payloads to client.
 *     - Verify that the packets are buffered as long as payload
 *       length matches gso_size.
 *     - When payload size > gso_size verify that the new payload is
 *       buffered and already buffered packets are sent to client
 *     - When payload size < gso_size verify that the new payload is
 *       sent along with the already buffered payloads.
 */
TEST_P(UdpListenerImplBatchWriterTest, SendData) {
  // Use a self address that is unlikely to be picked by source address discovery
  // algorithm if not specified in recvmsg/recvmmsg. Port is not taken into
  // consideration.
  Address::InstanceConstSharedPtr send_from_addr;
  if (version_ == Address::IpVersion::v4) {
    // Linux kernel regards any 127.x.x.x as local address. But Mac OS doesn't.
    send_from_addr = std::make_shared<Address::Ipv4Instance>(
#ifndef __APPLE__
        "127.1.2.3",
#else
        "127.0.0.1",
#endif
        server_socket_->localAddress()->ip()->port());
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
        server_socket_->localAddress()->ip()->port());
  }

  absl::FixedArray<std::string> payloads{"length7", "length7", "len<7",
                                         "length7", "length7", "length>7"};
  std::string internal_buffer("");
  std::string last_buffered("");
  std::list<std::string> pkts_to_send;

  for (const auto& payload : payloads) {
    Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
    buffer->add(payload);
    UdpSendData send_data{send_from_addr->ip(), *client_.localAddress(), *buffer};

    auto send_result = listener_->send(send_data);
    EXPECT_TRUE(send_result.ok()) << "send() failed : " << send_result.err_->getErrorDetails();

    // Verify udp_packet_writer stats for batch writing
    if (internal_buffer.length() == 0 ||       /* internal buffer is empty*/
        payload.compare(last_buffered) == 0) { /*len(payload) == gso_size*/

      pkts_to_send.emplace_back(payload);
      internal_buffer.append(payload);
      last_buffered = payload;

    } else if (payload.compare(last_buffered) < 0) { /*len(payload) < gso_size*/

      pkts_to_send.emplace_back(payload);
      internal_buffer.clear();
      last_buffered.clear();

    } else { /*len(payload) > gso_size*/

      internal_buffer = payload;
      last_buffered = payload;
    }

    EXPECT_EQ(listener_->udpPacketWriter()->getUdpPacketWriterStats().internal_buffer_size_.value(),
              internal_buffer.length());
    EXPECT_EQ(
        listener_->udpPacketWriter()->getUdpPacketWriterStats().last_buffered_msg_size_.value(),
        last_buffered.size());

    if (listener_->udpPacketWriter()->getUdpPacketWriterStats().sent_bytes_.value() != 0) {
      // Verify Correct content is received at the client
      size_t bytes_received = 0;
      for (const auto& pkt : pkts_to_send) {
        const uint64_t bytes_to_read = pkt.length();
        UdpRecvData data;
        client_.recv(data);
        bytes_received += data.buffer_->length();
        EXPECT_EQ(bytes_to_read, data.buffer_->length());
        EXPECT_EQ(send_from_addr->asString(), data.addresses_.peer_->asString());
        EXPECT_EQ(data.buffer_->toString(), pkt);
      }
      EXPECT_EQ(listener_->udpPacketWriter()->getUdpPacketWriterStats().sent_bytes_.value(),
                bytes_received);
      pkts_to_send.clear();
      if (last_buffered.length() != 0) {
        pkts_to_send.emplace_back(last_buffered);
      }
    }
  }

  // TODO(yugant):Test External Flush
}

// /** TODO(yugant): Clean this up or use it
//  * The send fails because the server_socket is created with bind=false.
//  */
// TEST_P(UdpListenerImplBatchWriterTest, SendDataError) {
//   Logger::StderrSinkDelegate stderr_sink(Logger::Registry::getSink()); // For coverage build.
//   const std::string payload("hello world");
//   Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
//   buffer->add(payload);
//   // send data to itself
//   UdpSendData send_data{send_to_addr_->ip(), *server_socket_->localAddress(), *buffer};

//   // Inject mocked OsSysCalls implementation to mock a write failure.
//   Api::MockOsSysCalls os_sys_calls;
//   TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
//   EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
//       .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOT_SUP}));
//   auto send_result = listener_->send(send_data);
//   EXPECT_FALSE(send_result.ok());
//   EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::NoSupport);
//   // Failed write shouldn't drain the data.
//   EXPECT_EQ(payload.length(), buffer->length());

//   ON_CALL(os_sys_calls, sendmsg(_, _, _))
//       .WillByDefault(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_INVAL}));
//   // EINVAL should cause RELEASE_ASSERT.
//   EXPECT_DEATH(listener_->send(send_data), "Invalid argument passed in");
// }

} // namespace
} // namespace Network
} // namespace Envoy
