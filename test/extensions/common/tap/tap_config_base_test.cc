#include "common/buffer/buffer_impl.h"

#include "extensions/common/tap/tap_config_base.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {
namespace {

TEST(BodyBytesToString, All) {
  {
    envoy::data::tap::v2alpha::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_request_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace, envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_BYTES);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().request_body_chunk().as_bytes());
  }

  {
    envoy::data::tap::v2alpha::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_request_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace,
                               envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().request_body_chunk().as_string());
  }

  {
    envoy::data::tap::v2alpha::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_response_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace,
                               envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().response_body_chunk().as_string());
  }

  {
    envoy::data::tap::v2alpha::TraceWrapper trace;
    trace.mutable_socket_streamed_trace_segment()
        ->mutable_event()
        ->mutable_read()
        ->mutable_data()
        ->set_as_bytes("hello");
    Utility::bodyBytesToString(trace,
                               envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.socket_streamed_trace_segment().event().read().data().as_string());
  }

  {
    envoy::data::tap::v2alpha::TraceWrapper trace;
    trace.mutable_socket_streamed_trace_segment()
        ->mutable_event()
        ->mutable_write()
        ->mutable_data()
        ->set_as_bytes("hello");
    Utility::bodyBytesToString(trace,
                               envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.socket_streamed_trace_segment().event().write().data().as_string());
  }
}

TEST(AddBufferToProtoBytes, All) {
  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v2alpha::Body body;
    Utility::addBufferToProtoBytes(body, 5, data, 4, 1);
    EXPECT_EQ("o", body.as_bytes());
    EXPECT_FALSE(body.truncated());
  }

  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v2alpha::Body body;
    Utility::addBufferToProtoBytes(body, 3, data, 0, 5);
    EXPECT_EQ("hel", body.as_bytes());
    EXPECT_TRUE(body.truncated());
  }

  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v2alpha::Body body;
    Utility::addBufferToProtoBytes(body, 100, data, 0, 5);
    EXPECT_EQ("hello", body.as_bytes());
    EXPECT_FALSE(body.truncated());
  }
}

TEST(TrimSlice, All) {
  {
    std::vector<Buffer::RawSlice> slices;
    Utility::trimSlices(slices, 0, 100);
    EXPECT_TRUE(slices.empty());
  }

  {
    std::vector<Buffer::RawSlice> slices = {{nullptr, 5}};
    Utility::trimSlices(slices, 0, 100);

    const std::vector<Buffer::RawSlice> expected{{nullptr, 5}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{nullptr, 5}};
    Utility::trimSlices(slices, 3, 3);

    const std::vector<Buffer::RawSlice> expected{{reinterpret_cast<void*>(0x3), 2}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{nullptr, 5}, {nullptr, 4}};
    Utility::trimSlices(slices, 3, 3);

    const std::vector<Buffer::RawSlice> expected{{reinterpret_cast<void*>(0x3), 2},
                                                 {reinterpret_cast<void*>(0x0), 1}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{nullptr, 5}, {nullptr, 4}};
    Utility::trimSlices(slices, 6, 3);

    const std::vector<Buffer::RawSlice> expected{{reinterpret_cast<void*>(0x5), 0},
                                                 {reinterpret_cast<void*>(0x1), 3}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{nullptr, 5}, {nullptr, 4}};
    Utility::trimSlices(slices, 0, 0);

    const std::vector<Buffer::RawSlice> expected{{reinterpret_cast<void*>(0x0), 0},
                                                 {reinterpret_cast<void*>(0x0), 0}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{nullptr, 5}, {nullptr, 4}};
    Utility::trimSlices(slices, 0, 3);

    const std::vector<Buffer::RawSlice> expected{{reinterpret_cast<void*>(0x0), 3},
                                                 {reinterpret_cast<void*>(0x0), 0}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{nullptr, 5}, {nullptr, 4}};
    Utility::trimSlices(slices, 1, 3);

    const std::vector<Buffer::RawSlice> expected{{reinterpret_cast<void*>(0x1), 3},
                                                 {reinterpret_cast<void*>(0x0), 0}};
    EXPECT_EQ(expected, slices);
  }
}

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
