#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#ifdef __GNUC__
#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"

#include "quiche/quic/test_tools/quic_mock_syscall_wrapper.h"

#pragma GCC diagnostic pop
#else
#include "quiche/quic/test_tools/quic_mock_syscall_wrapper.h"
#endif

#include "envoy/config/core/v3/base.pb.h"

#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/utility.h"

#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

#include "test/common/network/udp_listener_impl_test_base.h"
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

size_t getPacketLength(const msghdr* msg) {
  size_t length = 0;
  for (size_t i = 0; i < msg->msg_iovlen; ++i) {
    length += msg->msg_iov[i].iov_len;
  }
  return length;
}

class UdpListenerImplBatchWriterTest : public UdpListenerImplTestBase {
public:
  void SetUp() override {
    // Set listening socket options and set UdpGsoBatchWriter
    server_socket_->addOptions(SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(SocketOptionFactory::buildRxQueueOverFlowOptions());
    listener_ = std::make_unique<UdpListenerImpl>(
        dispatcherImpl(), server_socket_, listener_callbacks_, dispatcherImpl().timeSource());
    udp_packet_writer_ = std::make_unique<Quic::UdpGsoBatchWriter>(
        listener_->ioHandle(), listener_config_.listenerScope());
    ON_CALL(listener_callbacks_, udpPacketWriter()).WillByDefault(Return(udp_packet_writer_.get()));
  }
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
 *  3. Call UdpPacketWriter's External Flush
 *     - Verify that the internal buffer is emptied and its contents
 *       are delivered to the client.
 */
TEST_P(UdpListenerImplBatchWriterTest, SendData) {
  EXPECT_TRUE(udp_packet_writer_->isBatchMode());
  Address::InstanceConstSharedPtr send_from_addr = getUnlikelySourceAddress();

  absl::FixedArray<std::string> payloads{"length7", "length7", "len<7",
                                         "length7", "length7", "length>7"};
  std::string internal_buffer("");
  std::string first_buffered("");
  std::list<std::string> pkts_to_send;

  // Get initial value of total_bytes_sent
  uint64_t total_bytes_sent =
      listener_config_.listenerScope().counterFromString("total_bytes_sent").value();

  for (const auto& payload : payloads) {
    Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
    buffer->add(payload);
    UdpSendData send_data{send_from_addr->ip(), *client_.localAddress(), *buffer};

    auto send_result = listener_->send(send_data);
    EXPECT_TRUE(send_result.ok()) << "send() failed : " << send_result.err_->getErrorDetails();

    // Verify udp_packet_writer stats for batch writing
    if (internal_buffer.length() == 0 ||        /* internal buffer is empty*/
        payload.compare(first_buffered) == 0) { /*len(payload) == gso_size*/
      pkts_to_send.emplace_back(payload);
      internal_buffer.append(payload);
      first_buffered = payload;
    } else if (payload.compare(first_buffered) < 0) { /*len(payload) < gso_size*/
      pkts_to_send.emplace_back(payload);
      internal_buffer.clear();
      first_buffered.clear();
    } else { /*len(payload) > gso_size*/
      internal_buffer = payload;
      first_buffered = payload;
    }

    EXPECT_EQ(listener_config_.listenerScope()
                  .gaugeFromString("internal_buffer_size", Stats::Gauge::ImportMode::NeverImport)
                  .value(),
              internal_buffer.length());
    EXPECT_EQ(listener_config_.listenerScope()
                  .gaugeFromString("front_buffered_pkt_size", Stats::Gauge::ImportMode::NeverImport)
                  .value(),
              first_buffered.size());

    if (send_result.rc_ > 0) {
      // Verify Correct content is received at the client
      for (const auto& pkt : pkts_to_send) {
        const uint64_t bytes_to_read = pkt.length();
        UdpRecvData data;
        client_.recv(data);
        total_bytes_sent += data.buffer_->length();
        EXPECT_EQ(bytes_to_read, data.buffer_->length());
        EXPECT_EQ(send_from_addr->asString(), data.addresses_.peer_->asString());
        EXPECT_EQ(data.buffer_->toString(), pkt);
      }
      EXPECT_EQ(listener_config_.listenerScope().counterFromString("total_bytes_sent").value(),
                total_bytes_sent);
      pkts_to_send.clear();
      if (first_buffered.length() != 0) {
        pkts_to_send.emplace_back(first_buffered);
      }
    }
  }

  // Test External Flush
  auto flush_result = udp_packet_writer_->flush();
  EXPECT_TRUE(flush_result.ok());
  EXPECT_EQ(listener_config_.listenerScope()
                .gaugeFromString("internal_buffer_size", Stats::Gauge::ImportMode::NeverImport)
                .value(),
            0);
  EXPECT_EQ(listener_config_.listenerScope()
                .gaugeFromString("front_buffered_pkt_size", Stats::Gauge::ImportMode::NeverImport)
                .value(),
            0);

  const uint64_t bytes_flushed = payloads.back().length();
  UdpRecvData received_flush_data;
  client_.recv(received_flush_data);
  total_bytes_sent += received_flush_data.buffer_->length();

  EXPECT_EQ(listener_config_.listenerScope().counterFromString("total_bytes_sent").value(),
            total_bytes_sent);
  EXPECT_EQ(bytes_flushed, received_flush_data.buffer_->length());
  EXPECT_EQ(send_from_addr->asString(), received_flush_data.addresses_.peer_->asString());
  EXPECT_EQ(received_flush_data.buffer_->toString(), payloads.back());
}

/**
 * Tests UDP Packet writer behavior when socket is write-blocked.
 * 1. Setup the udp_listener and have a payload buffered in the internal buffer.
 * 2. Then set the socket to return EWOULDBLOCK error on sendmsg and write a
 *    different sized buffer to the packet writer.
 *    - Ensure that a buffer shorter than the initial buffer is added to the
 *      Internal Buffer.
 *    - A buffer longer than the initial buffer should not get appended to the
 *      Internal Buffer.
 */
TEST_P(UdpListenerImplBatchWriterTest, WriteBlocked) {
  // Quic Mock Objects
  quic::test::MockQuicSyscallWrapper os_sys_calls;
  quic::ScopedGlobalSyscallWrapperOverride os_calls(&os_sys_calls);

  // The initial payload to be buffered
  std::string initial_payload("length7");
  Buffer::InstancePtr initial_buffer(new Buffer::OwnedImpl());
  initial_buffer->add(initial_payload);
  UdpSendData initial_send_data{send_to_addr_->ip(), *server_socket_->localAddress(),
                                *initial_buffer};

  // Get initial value of total_bytes_sent
  uint64_t total_bytes_sent =
      listener_config_.listenerScope().counterFromString("total_bytes_sent").value();

  // Possible followup payloads to be sent after the initial payload
  absl::FixedArray<std::string> followup_payloads{"length<7", "len<7"};

  for (const auto& followup_payload : followup_payloads) {
    std::string internal_buffer("");

    // First have initial payload added to the udp_packet_writer's internal buffer.
    auto send_result = listener_->send(initial_send_data);
    internal_buffer.append(initial_payload);
    EXPECT_TRUE(send_result.ok());
    EXPECT_FALSE(udp_packet_writer_->isWriteBlocked());
    EXPECT_EQ(listener_config_.listenerScope()
                  .gaugeFromString("internal_buffer_size", Stats::Gauge::ImportMode::NeverImport)
                  .value(),
              initial_payload.length());
    EXPECT_EQ(listener_config_.listenerScope()
                  .gaugeFromString("front_buffered_pkt_size", Stats::Gauge::ImportMode::NeverImport)
                  .value(),
              initial_payload.length());
    EXPECT_EQ(listener_config_.listenerScope().counterFromString("total_bytes_sent").value(),
              total_bytes_sent);

    // Mock the socket to be write blocked on sendmsg syscall
    EXPECT_CALL(os_sys_calls, Sendmsg(_, _, _))
        .WillOnce(Invoke([](int /*sockfd*/, const msghdr* /*msg*/, int /*flags*/) {
          errno = EWOULDBLOCK;
          return -1;
        }));

    // Now send the followup payload
    Buffer::InstancePtr followup_buffer(new Buffer::OwnedImpl());
    followup_buffer->add(followup_payload);
    UdpSendData followup_send_data{send_to_addr_->ip(), *server_socket_->localAddress(),
                                   *followup_buffer};
    send_result = listener_->send(followup_send_data);

    // The followup payload should only get buffered if it is shorter than initial payload
    if (followup_payload.length() < initial_payload.length()) {
      EXPECT_TRUE(send_result.ok());
      // TODO(yugant): This flag should be true here, but currently it is incorrectly set in
      // quiche code. Change this to True, once this issue is fixed in quiche.
      EXPECT_FALSE(udp_packet_writer_->isWriteBlocked());
      internal_buffer.append(followup_payload);
    } else if (followup_payload.length() > initial_payload.length()) {
      EXPECT_FALSE(send_result.ok());
      EXPECT_TRUE(udp_packet_writer_->isWriteBlocked());
    }
    EXPECT_EQ(listener_config_.listenerScope()
                  .gaugeFromString("front_buffered_pkt_size", Stats::Gauge::ImportMode::NeverImport)
                  .value(),
              initial_payload.length());
    EXPECT_EQ(listener_config_.listenerScope().counterFromString("total_bytes_sent").value(),
              total_bytes_sent);
    EXPECT_EQ(listener_config_.listenerScope()
                  .gaugeFromString("internal_buffer_size", Stats::Gauge::ImportMode::NeverImport)
                  .value(),
              internal_buffer.length());

    // Mock the socket implement successful sendmsg
    EXPECT_CALL(os_sys_calls, Sendmsg(_, _, _))
        .WillOnce(Invoke([&](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
          EXPECT_EQ(internal_buffer.length(), getPacketLength(msg));
          return internal_buffer.length();
        }));

    // Reset write blocked status, and verify correct buffer is flushed
    udp_packet_writer_->setWritable();
    auto flush_result = udp_packet_writer_->flush();
    EXPECT_TRUE(flush_result.ok());
    EXPECT_FALSE(udp_packet_writer_->isWriteBlocked());
    EXPECT_EQ(listener_config_.listenerScope()
                  .gaugeFromString("internal_buffer_size", Stats::Gauge::ImportMode::NeverImport)
                  .value(),
              0);
    total_bytes_sent += internal_buffer.length();
    EXPECT_EQ(listener_config_.listenerScope().counterFromString("total_bytes_sent").value(),
              total_bytes_sent);
  }
}

} // namespace
} // namespace Network
} // namespace Envoy
