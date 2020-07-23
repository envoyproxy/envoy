#include <cstddef>
#include <memory>
#include <string>
#include <vector>

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

class UdpListenerImplBatchWriterTest : public UdpListenerImplTestBase {
public:
  void SetUp() override {
    // Set listening socket options and set UdpGsoBatchWriter
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

  Address::InstanceConstSharedPtr send_from_addr = getUnlikelySourceAddress();

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

  // Test External Flush
  auto flush_result = listener_->udpPacketWriter()->flush();
  EXPECT_EQ(listener_->udpPacketWriter()->getUdpPacketWriterStats().internal_buffer_size_.value(),
            0);
  EXPECT_EQ(listener_->udpPacketWriter()->getUdpPacketWriterStats().last_buffered_msg_size_.value(),
            0);
  const uint64_t bytes_flushed = payloads.back().length();
  UdpRecvData received_flush_data;
  client_.recv(received_flush_data);
  EXPECT_EQ(listener_->udpPacketWriter()->getUdpPacketWriterStats().sent_bytes_.value(),
            received_flush_data.buffer_->length());
  EXPECT_EQ(bytes_flushed, received_flush_data.buffer_->length());
  EXPECT_EQ(send_from_addr->asString(), received_flush_data.addresses_.peer_->asString());
  EXPECT_EQ(received_flush_data.buffer_->toString(), payloads.back());
}

} // namespace
} // namespace Network
} // namespace Envoy
