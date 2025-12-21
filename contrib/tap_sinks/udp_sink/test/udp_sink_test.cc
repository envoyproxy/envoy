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

  // case for format PROTO_BINARY_LENGTH_DELIMITED
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED);
}

// In order to control the return value of writePacket, then
// re Mock class UdpPacketWriter and not use existing Network::MockUdpPacketWriter
class MockUdpPacketWriterNew : public Network::UdpPacketWriter {
public:
  MockUdpPacketWriterNew(bool isReturnOk) : isReturnOkForwritePacket_(isReturnOk) {};
  ~MockUdpPacketWriterNew() override {};

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

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsString) {
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

TEST_F(UdpTapSinkTest, TestSubmitTraceSendNotOkForJsonBodyAsString) {
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

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkforProtoText) {
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
                            envoy::config::tap::v3::OutputSink::PROTO_TEXT);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkforProtoBinary) {
  // Construct UdpTapSink object.
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle.
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);
  // Case 1: the return of SerializeToArray() is true.
  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::PROTO_BINARY);

  // Case 2 the return of SerializeToArray() is false.
  // Google Test doesn't support mocking this kind of case with its current capabilities.
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForProtoBinaryReadEvForBigUdpMsg) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  // Set the buffer size limitation
  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(16);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_seconds(1);
  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_nanos(1);

  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");

  std::string body_data("01234567890123456789012345678901234567890123456789");
  dst_streamed_trace.mutable_event()->mutable_read()->mutable_data()->set_as_bytes(
      body_data.data(), body_data.size());
  dst_streamed_trace.mutable_event()->set_seq_num(1);

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::PROTO_BINARY);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesReadEvForBigUdpMsg) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(16);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_seconds(1);
  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_nanos(1);

  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");

  std::string body_data("01234567890123456789012345678901234567890123456789");
  dst_streamed_trace.mutable_event()->mutable_read()->mutable_data()->set_as_bytes(
      body_data.data(), body_data.size());
  dst_streamed_trace.mutable_event()->set_seq_num(1);

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesWriteEvForBigUdpMsg) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(16);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_seconds(1);
  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_nanos(1);

  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");

  std::string body_data("01234567890123456789012345678901234567890123456789");
  dst_streamed_trace.mutable_event()->mutable_write()->mutable_data()->set_as_bytes(
      body_data.data(), body_data.size());
  dst_streamed_trace.mutable_event()->set_seq_num(1);

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesWriteEvTwoForBigUdpMsg) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  // Set the buffer size limitation and make sure the data length is 20
  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(10);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_seconds(1);
  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_nanos(1);

  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");

  std::string body_data("01234567890123456789");
  dst_streamed_trace.mutable_event()->mutable_write()->mutable_data()->set_as_bytes(
      body_data.data(), body_data.size());
  dst_streamed_trace.mutable_event()->set_seq_num(1);

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsStringReadEvForBigUdpMsg) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsString(16);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_seconds(1);
  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_nanos(1);
  dst_streamed_trace.mutable_event()->set_seq_num(1);

  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");

  std::string body_data("01234567890123456789012345678901234567890123456789");
  dst_streamed_trace.mutable_event()->mutable_read()->mutable_data()->set_as_string(
      body_data.data(), body_data.size());
  dst_streamed_trace.mutable_event()->set_seq_num(1);
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsStringWriteEvForBigUdpMsg) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsString(16);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_seconds(1);
  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_nanos(1);

  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");

  std::string body_data("01234567890123456789012345678901234567890123456789");
  dst_streamed_trace.mutable_event()->mutable_write()->mutable_data()->set_as_string(
      body_data.data(), body_data.size());

  dst_streamed_trace.mutable_event()->set_seq_num(1);
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesWriteEvForSmallUdpMsg) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(16);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_seconds(1);
  dst_streamed_trace.mutable_event()->mutable_timestamp()->set_nanos(1);

  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  dst_streamed_trace.mutable_connection()
      ->mutable_local_address()
      ->mutable_socket_address()
      ->set_address("127.0.0.1");

  std::string body_data("012345678901");
  dst_streamed_trace.mutable_event()->mutable_write()->mutable_data()->set_as_bytes(
      body_data.data(), body_data.size());

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesWriteEventsSmallData) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(128);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  // Event0
  auto& event = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                     ->mutable_events()
                     ->add_events();

  event.mutable_timestamp()->set_seconds(1);
  event.mutable_timestamp()->set_nanos(1);
  std::string body_data("012345");
  event.mutable_write()->mutable_data()->set_as_bytes(body_data.data(), body_data.size());
  event.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  // Event1
  auto& event1 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event1.mutable_timestamp()->set_seconds(2);
  event1.mutable_timestamp()->set_nanos(2);
  std::string body_data1("789");
  event1.mutable_write()->mutable_data()->set_as_bytes(body_data1.data(), body_data1.size());
  event1.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event1.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesWriteEventsBigDataOneEvent) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(64);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  // Event0
  auto& event = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                     ->mutable_events()
                     ->add_events();

  event.mutable_timestamp()->set_seconds(1);
  event.mutable_timestamp()->set_nanos(1);
  std::string body_data("01234567890123456789012345678901234567890123456789012345678901234567890123"
                        "456789012345678901234567890123456789012345678901234567890123456789");
  event.mutable_write()->mutable_data()->set_as_bytes(body_data.data(), body_data.size());
  event.set_seq_num(1);
  event.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesWriteTwoBigDataEvents) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(64);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  // Event0
  auto& event = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                     ->mutable_events()
                     ->add_events();

  event.mutable_timestamp()->set_seconds(1);
  event.mutable_timestamp()->set_nanos(1);
  std::string body_data("01234567890123456789012345678901234567890123456789012345678901234567890123"
                        "456789012345678901234567890123456789012345678901234567890123456789");
  event.mutable_write()->mutable_data()->set_as_bytes(body_data.data(), body_data.size());
  event.set_seq_num(1);
  event.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event1 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event1.mutable_timestamp()->set_seconds(1);
  event1.mutable_timestamp()->set_nanos(1);
  std::string body_data1("12345678901");
  event1.mutable_write()->mutable_data()->set_as_bytes(body_data1.data(), body_data1.size());
  event1.set_seq_num(1 + body_data.size());
  event1.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event1.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event2 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event2.mutable_timestamp()->set_seconds(1);
  event2.mutable_timestamp()->set_nanos(1);
  std::string body_data2(
      "01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901"
      "23456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123"
      "456789012345678901234567890123456789");
  event2.mutable_write()->mutable_data()->set_as_bytes(body_data2.data(), body_data2.size());
  event2.set_seq_num(1 + body_data.size() + body_data1.size());
  event2.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event2.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event3 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event3.mutable_timestamp()->set_seconds(1);
  event3.mutable_timestamp()->set_nanos(1);
  std::string body_data3("1234567890123");
  event3.mutable_write()->mutable_data()->set_as_bytes(body_data3.data(), body_data3.size());
  event3.set_seq_num(1 + body_data.size() + body_data1.size() + body_data2.size());
  event3.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event3.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesWriteTwoEvents) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(64);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  auto& event1 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event1.mutable_timestamp()->set_seconds(1);
  event1.mutable_timestamp()->set_nanos(1);
  std::string body_data1("1234567890123456789012345678");
  event1.mutable_write()->mutable_data()->set_as_bytes(body_data1.data(), body_data1.size());
  event1.set_seq_num(1);
  event1.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event1.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event3 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event3.mutable_timestamp()->set_seconds(1);
  event3.mutable_timestamp()->set_nanos(1);
  std::string body_data3("12345678901");
  event3.mutable_write()->mutable_data()->set_as_bytes(body_data3.data(), body_data3.size());
  event3.set_seq_num(1 + body_data1.size());
  event3.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event3.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesReadOneBigTwoSmallEvent) {
  // Construct UdpTapSink object
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(16);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  auto& event1 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event1.mutable_timestamp()->set_seconds(1);
  event1.mutable_timestamp()->set_nanos(1);
  std::string body_data1("1234567890123456789012345678");
  event1.mutable_read()->mutable_data()->set_as_bytes(body_data1.data(), body_data1.size());
  event1.set_seq_num(1);
  event1.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event1.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event2 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event2.mutable_timestamp()->set_seconds(2);
  event2.mutable_timestamp()->set_nanos(2);
  std::string body_data2("123456782");
  event2.mutable_read()->mutable_data()->set_as_bytes(body_data2.data(), body_data2.size());
  event2.set_seq_num(1 + body_data1.size());
  event2.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event2.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event3 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event3.mutable_timestamp()->set_seconds(2);
  event3.mutable_timestamp()->set_nanos(2);
  std::string body_data3("123456783");
  event3.mutable_read()->mutable_data()->set_as_bytes(body_data3.data(), body_data3.size());
  event2.set_seq_num(1 + body_data1.size() + body_data2.size());
  event3.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event3.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsBytesReadOneBigTwoSmallEvent2) {
  // The left two small events which the size is less than limitation.
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsBytes(128);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  auto& event1 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event1.mutable_timestamp()->set_seconds(1);
  event1.mutable_timestamp()->set_nanos(1);
  std::string body_data1("1234567890123456789012345678901234567890123456789012345678901234567890123"
                         "4567890123456789012345678901234567890123456789012345678901234567890");
  event1.mutable_read()->mutable_data()->set_as_bytes(body_data1.data(), body_data1.size());
  event1.set_seq_num(1);
  event1.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event1.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event2 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event2.mutable_timestamp()->set_seconds(2);
  event2.mutable_timestamp()->set_nanos(2);
  std::string body_data2("123456782");
  event2.mutable_read()->mutable_data()->set_as_bytes(body_data2.data(), body_data2.size());
  event2.set_seq_num(1 + body_data1.size());
  event2.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event2.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event3 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event3.mutable_timestamp()->set_seconds(3);
  event3.mutable_timestamp()->set_nanos(3);
  std::string body_data3("123456783");
  event2.set_seq_num(1 + body_data1.size() + body_data2.size());
  event3.mutable_read()->mutable_data()->set_as_bytes(body_data3.data(), body_data3.size());
  event3.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event3.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOkForJsonBodyAsStrReadTwoSmallandOneBigEvent) {
  // The last event which the body size is bigger than the limitation.
  envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");

  UtSpecialUdpTapSink loc_udp_tap_sink(loc_udp_sink);

  loc_udp_tap_sink.setUdpMaxSendMsgDataSizeAsString(128);

  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  loc_udp_tap_sink.replaceOrigUdpPacketWriter(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  TapCommon::PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kCustomSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  // Try to set value in streamed trace
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *local_buffered_trace->mutable_socket_streamed_trace_segment();
  dst_streamed_trace.set_trace_id(99);

  auto& event1 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event1.mutable_timestamp()->set_seconds(1);
  event1.mutable_timestamp()->set_nanos(1);
  std::string body_data1("123456781");
  event1.mutable_read()->mutable_data()->set_as_bytes(body_data1.data(), body_data1.size());
  event1.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event1.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event2 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event2.mutable_timestamp()->set_seconds(2);
  event2.mutable_timestamp()->set_nanos(2);
  std::string body_data2("123456782");
  event2.mutable_read()->mutable_data()->set_as_bytes(body_data2.data(), body_data2.size());
  event2.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event2.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  auto& event3 = *local_buffered_trace->mutable_socket_streamed_trace_segment()
                      ->mutable_events()
                      ->add_events();

  event3.mutable_timestamp()->set_seconds(3);
  event3.mutable_timestamp()->set_nanos(3);
  std::string body_data3(
      "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012"
      "34     5678901234567890123456789012345678901234567890");
  event3.mutable_read()->mutable_data()->set_as_bytes(body_data3.data(), body_data3.size());
  event3.mutable_connection()->mutable_local_address()->mutable_socket_address()->set_address(
      "127.0.0.1");
  event3.mutable_connection()->mutable_remote_address()->mutable_socket_address()->set_address(
      "127.0.0.1");

  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
}

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
