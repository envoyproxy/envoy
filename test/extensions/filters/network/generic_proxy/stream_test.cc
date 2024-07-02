#include <memory>

#include "source/extensions/filters/network/generic_proxy/interface/stream.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

bool operator==(const FrameFlags& lhs, const FrameFlags& rhs) {
  return lhs.streamId() == rhs.streamId() && lhs.frameTags() == rhs.frameTags() &&
         lhs.rawFlags() == rhs.rawFlags();
}

class TestResponseHeaderFrame : public ResponseHeaderFrame {
public:
  absl::string_view protocol() const override { return "test"; }
};

class TestRequestHeaderFrame : public RequestHeaderFrame {
public:
  absl::string_view protocol() const override { return "test"; }
};

TEST(StreamInterfaceTest, StreamInterfaceTest) {
  {
    // FrameFlags Test.
    FrameFlags flags; // All flags are default values.
    EXPECT_EQ(flags.streamId(), 0);
    EXPECT_EQ(flags.frameTags(), 0);
    EXPECT_EQ(flags.endStream(), true);
    EXPECT_EQ(flags.oneWayStream(), false);
    EXPECT_EQ(flags.drainClose(), false);
    EXPECT_EQ(flags.heartbeat(), false);

    FrameFlags flags1(1, FrameFlags::FLAG_EMPTY, 0);
    EXPECT_EQ(flags1.streamId(), 1);
    EXPECT_EQ(flags1.frameTags(), 0);
    EXPECT_EQ(flags1.endStream(), false);
    EXPECT_EQ(flags1.oneWayStream(), false);
    EXPECT_EQ(flags1.drainClose(), false);
    EXPECT_EQ(flags1.heartbeat(), false);

    FrameFlags flags2(2,
                      FrameFlags::FLAG_ONE_WAY | FrameFlags::FLAG_HEARTBEAT |
                          FrameFlags::FLAG_END_STREAM | FrameFlags::FLAG_DRAIN_CLOSE,
                      3);
    EXPECT_EQ(flags2.streamId(), 2);
    EXPECT_EQ(flags2.frameTags(), 3);
    EXPECT_EQ(flags2.endStream(), true);
    EXPECT_EQ(flags2.oneWayStream(), true);
    EXPECT_EQ(flags2.drainClose(), true);
    EXPECT_EQ(flags2.heartbeat(), true);
  }

  TestRequestHeaderFrame request_frame;

  EXPECT_EQ(request_frame.protocol(), "test");

  // Default implementation does nothing.
  EXPECT_TRUE(request_frame.frameFlags() == FrameFlags());
  EXPECT_EQ(request_frame.path(), "");
  EXPECT_EQ(request_frame.method(), "");
  EXPECT_EQ(request_frame.host(), "");
  request_frame.set("key", "val");
  EXPECT_EQ(absl::nullopt, request_frame.get("key"));
  request_frame.erase("key");
  request_frame.forEach(nullptr);

  TestResponseHeaderFrame response_frame;

  EXPECT_EQ(response_frame.protocol(), "test");
  response_frame.frameFlags();

  // Default implementation does nothing.
  EXPECT_TRUE(response_frame.frameFlags() == FrameFlags());
  EXPECT_EQ(response_frame.status().code(), 0);
  EXPECT_EQ(response_frame.status().ok(), true);
  response_frame.set("key", "val");
  EXPECT_EQ(absl::nullopt, response_frame.get("key"));
  response_frame.erase("key");
  response_frame.forEach(nullptr);
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
