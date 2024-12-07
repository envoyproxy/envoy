#include <vector>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"
#include "envoy/network/udp_packet_writer_handler.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/common/tap/tap_config_base.h"

#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {
namespace {

TEST(BodyBytesToString, All) {
  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_request_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().request_body_chunk().as_bytes());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_request_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().request_body_chunk().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_response_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().response_body_chunk().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_socket_streamed_trace_segment()
        ->mutable_event()
        ->mutable_read()
        ->mutable_data()
        ->set_as_bytes("hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.socket_streamed_trace_segment().event().read().data().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_socket_streamed_trace_segment()
        ->mutable_event()
        ->mutable_write()
        ->mutable_data()
        ->set_as_bytes("hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.socket_streamed_trace_segment().event().write().data().as_string());
  }
}

TEST(AddBufferToProtoBytes, All) {
  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v3::Body body;
    Utility::addBufferToProtoBytes(body, 5, data, 4, 1);
    EXPECT_EQ("o", body.as_bytes());
    EXPECT_FALSE(body.truncated());
  }

  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v3::Body body;
    Utility::addBufferToProtoBytes(body, 3, data, 0, 5);
    EXPECT_EQ("hel", body.as_bytes());
    EXPECT_TRUE(body.truncated());
  }

  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v3::Body body;
    Utility::addBufferToProtoBytes(body, 100, data, 0, 5);
    EXPECT_EQ("hello", body.as_bytes());
    EXPECT_FALSE(body.truncated());
  }
}

TEST(TrimSlice, All) {
  std::string slice_mem = "static base slice memory that is long enough";
  void* test_base = static_cast<void*>(&slice_mem[0]);
  {
    std::vector<Buffer::RawSlice> slices;
    Utility::trimSlices(slices, 0, 100);
    EXPECT_TRUE(slices.empty());
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}};
    Utility::trimSlices(slices, 0, 100);

    const std::vector<Buffer::RawSlice> expected{{test_base, 5}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}};
    Utility::trimSlices(slices, 3, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[3]), 2}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 3, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[3]), 2},
                                                 {static_cast<void*>(&slice_mem[0]), 1}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 6, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[5]), 0},
                                                 {static_cast<void*>(&slice_mem[1]), 3}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 0, 0);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[0]), 0},
                                                 {static_cast<void*>(&slice_mem[0]), 0}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 0, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[0]), 3},
                                                 {static_cast<void*>(&slice_mem[0]), 0}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 1, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[1]), 3},
                                                 {static_cast<void*>(&slice_mem[0]), 0}};
    EXPECT_EQ(expected, slices);
  }
}

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
  envoy::config::tap::v3::UDPSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);
  EXPECT_TRUE(!loc_udp_tap_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestConstructBadUDPAddress) {
  envoy::config::tap::v3::UDPSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(65539);
  socket_address->set_address("127.800.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);
  EXPECT_TRUE(!loc_udp_tap_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestConstructGoodUDPAddress) {
  envoy::config::tap::v3::UDPSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);
  EXPECT_TRUE(loc_udp_tap_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestSubmitTraceNotUdpPacketWriter) {
  envoy::config::tap::v3::UDPSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);

  // Create UdpTapSinkHandle
  PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kUdpSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceForNotSUpportedFormat) {
  // Construct UdpTapSink object
  envoy::config::tap::v3::UDPSink loc_udp_sink;
  auto* socket_address = loc_udp_sink.mutable_udp_address();
  socket_address->set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  socket_address->set_port_value(8080);
  socket_address->set_address("127.0.0.1");
  UdpTapSink loc_udp_tap_sink(loc_udp_sink);

  // Create UdpTapSinkHandle
  PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kUdpSink);

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

TEST_F(UdpTapSinkTest, TestSubmitTraceSendOk) {
  // Construct UdpTapSink object
  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(true);
  UdpTapSink loc_udp_tap_sink(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kUdpSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
}

TEST_F(UdpTapSinkTest, TestSubmitTraceSendNotOk) {
  // Construct UdpTapSink object
  std::unique_ptr<MockUdpPacketWriterNew> local_UdpPacketWriter =
      std::make_unique<MockUdpPacketWriterNew>(false);
  UdpTapSink loc_udp_tap_sink(std::move(local_UdpPacketWriter));

  // Create UdpTapSinkHandle
  PerTapSinkHandlePtr local_handle =
      loc_udp_tap_sink.createPerTapSinkHandle(99, ProtoOutputSink::OutputSinkTypeCase::kUdpSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
      Extensions::Common::Tap::makeTraceWrapper();
  local_handle->submitTrace(std::move(local_buffered_trace),
                            envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
}
} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
