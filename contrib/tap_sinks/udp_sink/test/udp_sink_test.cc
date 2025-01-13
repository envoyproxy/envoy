#include <vector>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"
#include "envoy/network/udp_packet_writer_handler.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"

#include "test/mocks/network/mocks.h"

#include "contrib/tap_sinks/udp_sink/source/udp_sink_impl.h"

namespace Envoy {
namespace Extensions {
namespace TapSinks {
namespace UDP {

// Test Udp sink
using ::testing::_;
using ::testing::Return;

class UdpTapSinkTest : public testing::Test {
protected:
  UdpTapSinkTest() {}
  ~UdpTapSinkTest() {}

public:
  envoy::config::core::v3::SocketAddress socket_address_;
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;
};

TEST_F(UdpTapSinkTest, TestConstructNotSupportTCPprotocol) {
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);
  EXPECT_TRUE(!loc_udp_tap_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestConstructBadUDPAddress) {
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(65539);
  socket_address->set_address("127.800.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);
  EXPECT_TRUE(!loc_udp_tap_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestConstructGoodUDPAddress) {
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);
  EXPECT_TRUE(loc_udp_tap_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestSubmitTraceNotUdpPacketWriter) {
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceForNotSUpportedFormat) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // case1 format PROTO_BINARY
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::PROTO_BINARY);
  // case2 format PROTO_BINARY_LENGTH_DELIMITED
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED);
  // case3 format PROTO_TEXT
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::PROTO_TEXT);
}

// In order to control the return value of writePacket, then
// re Mock class UdpPacketWriter and not use existing Network::MockUdpPacketWriter
class MockUdpPacketWriterNew : public Network::UdpPacketWriter {
public:
  MockUdpPacketWriterNew(bool isReturnOk) : isReturnOkForwritePacket_(isReturnOk){};
  ~MockUdpPacketWriterNew() override{};

  Api::IoCallUint64Result writePacket(const Buffer::Instance& buffer,
                                      const Network::Address::Ip* local_ip,
                                      const Network::Address::Instance& peer_address) override {
    (void)buffer;
    (void)local_ip;
    (void)peer_address;
    if (isReturnOkForwritePacket_) {
      return Api::IoCallUint64Result(99, Api::IoError::none());
    } else {
      return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
    }
  }
  MOCK_METHOD(bool, isWriteBlocked, (), (const));
  MOCK_METHOD(void, setWritable, ());
  MOCK_METHOD(uint64_t, getMaxPacketSize, (const Network::Address::Instance& peer_address),
              (const));
  MOCK_METHOD(bool, isBatchMode, (), (const));
  MOCK_METHOD(Network::UdpPacketWriterBuffer, getNextWriteLocation,
              (const Network::Address::Ip* local_ip,
               const Network::Address::Instance& peer_address));
  MOCK_METHOD(Api::IoCallUint64Result, flush, ());

private:
  const bool isReturnOkForwritePacket_;
};

class UtSpecialUdpTapSink : public UdpTapSink {
public:
  UtSpecialUdpTapSink(const envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink& config)
      : UdpTapSink(config) {}
  ~UtSpecialUdpTapSink() {}
  void replaceOrigUdpPacketWriter(Network::UdpPacketWriterPtr&& utUdpPacketWriter) {
    udp_packet_writer_ = std::move(utUdpPacketWriter);
  }
};

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOk) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendNotOk) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(false);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
}

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
