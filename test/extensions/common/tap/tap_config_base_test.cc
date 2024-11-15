#include <vector>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/common/tap/tap_config_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
using ::testing::Return; 
using ::testing::_; 
// Mock the method of class UdpPacketWriter
// Ignore other method
class mockUdpPacketWriter : public UdpPacketWriter {
public:
  MOCK_METHOD(Api::IoCallUint64Result,
              writePacket, (const Buffer::Instance&, const Address::Ip*, const Address::Instance&),
			  (override));
};

class UdpTapSinkTest : public testing::Test {
protected:
  UdpTapSinkTest() {}
  ~UdpTapSinkTest() {}
  void SetUp() overide {}
  void TearDown() overide {}
publish:
  envoy::config::core::v3::SocketAddress socket_address_;
};

TEST_F(UdpTapSinkTest, TestConstructNotSupportTCPprotocol) {
  socket_address_.set_protocol(envoy::config::core::v3::SocketAddress_Protocol_TCP);
  envoy::config::tap::v3::UDPSink loc_udp_sink(socket_address_);
  UdpTapSink loc_udp_sink(loc_udp_sink);
  EXPECT_TRUE(!loc_udp_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestConstructBadUDPAddress) {
  socket_address_.set_protocol(envoy::config::core::v3::SocketAddress_Protocol_UDP);
  envoy::config::tap::v3::UDPSink loc_udp_sink(socket_address_);
  MOCK_METHOD(Address::InstanceConstSharedPtr,
              mock_parseInternetAddressNoThrow,
              (const std::string&, uint16_t, bool));
  ON_CALL(Network::Utility::parseInternetAddressNoThrow, mock_parseInternetAddressNoThrow)
         .willByDefault(Return(nullptr)); 
  UdpTapSink loc_udp_sink(loc_udp_sink);
  EXPECT_TRUE(!loc_udp_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestConstructGoodUDPAddress) {
  socket_address_.set_protocol(envoy::config::core::v3::SocketAddress_Protocol_UDP);
  socket_address_.set_port_value(8080);
  socket_address_.set_allocated_address("127.0.0.1");
  envoy::config::tap::v3::UDPSink loc_udp_sink(socket_address_);
  UdpTapSink loc_udp_sink(loc_udp_sink);
  EXPECT_TRUE(loc_udp_sink.isUdpPacketWriterCreated());
}

TEST_F(UdpTapSinkTest, TestSubmitTraceNotUdpPacketWriter) {
  socket_address_.set_protocol(envoy::config::core::v3::SocketAddress_Protocol_TCP);
  envoy::config::tap::v3::UDPSink loc_udp_sink(socket_address_);
  UdpTapSink loc_udp_sink(loc_udp_sink);

  // Create UdpTapSinkHandle
  PerTapSinkHandlePtr local_handle = loc_udp_sink.createPerTapSinkHandle(99,
       ProtoOutputSink::OutputSinkTypeCase::kUdpSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
    Extensions::Common::Tap::makeTraceWrapper();
  local_handle.submitTrace(std::move(local_buffered_trace),
	envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
}

TEST_F(UdpTapSinkTest, TestSubmitTrace) {
  // Construct UdpTapSink object
  socket_address_.set_protocol(envoy::config::core::v3::SocketAddress_Protocol_UDP);
  socket_address_.set_port_value(8080);
  socket_address_.set_allocated_address("127.0.0.1");
  envoy::config::tap::v3::UDPSink loc_udp_sink(socket_address_);
  UdpTapSink loc_udp_sink(loc_udp_sink);
  
  // Create UdpTapSinkHandle
  PerTapSinkHandlePtr local_handle = loc_udp_sink.createPerTapSinkHandle(99,
       ProtoOutputSink::OutputSinkTypeCase::kUdpSink);

  Extensions::Common::Tap::TraceWrapperPtr local_buffered_trace =
    Extensions::Common::Tap::makeTraceWrapper();
  // case1 format PROTO_BINARY
  local_handle.submitTrace(std::move(local_buffered_trace),
	envoy::config::tap::v3::OutputSink::PROTO_BINARY);
  // case2 format PROTO_BINARY_LENGTH_DELIMITED
  local_handle.submitTrace(std::move(local_buffered_trace),
	envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED);
  // case3 format PROTO_TEXT
  local_handle.submitTrace(std::move(local_buffered_trace),
	envoy::config::tap::v3::OutputSink::PROTO_TEXT);
  //Test writer
  mockUdpPacketWriter local_UdpPacketWriter;
  // case3.1 format JSON_BODY_AS_STRING, write_result.ok()
  EXPECT_CALL(local_UdpPacketWriter, writePacket(_,_,_))
           .Times(1)
	   .WillOnce(Return(Api::ioCallUint64ResultNoError()));
  local_handle.submitTrace(std::move(local_buffered_trace),
	envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
  // case3.2 format JSON_BODY_AS_STRING, not write_result.ok()
  EXPECT_CALL(local_UdpPacketWriter, writePacket(_,_,_))
           .Times(1)
	   .WillOnce(Return(Network::IoSocketError::ioResultSocketInvalidAddress()));
  local_handle.submitTrace(std::move(local_buffered_trace),
	envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);

}

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
