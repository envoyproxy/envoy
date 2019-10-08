#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/grpc_streaming/message_counter.h"

#include "test/common/buffer/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStreaming {

namespace {

TEST(MessageCounterTest, IncrementMessageCounter) {
  {
    Buffer::OwnedImpl buffer;
    GrpcMessageCounter counter;
    EXPECT_EQ(0, IncrementMessageCounter(buffer, &counter));
    EXPECT_EQ(counter.state, GrpcMessageCounter::GrpcReadState::ExpectByte0);
    EXPECT_EQ(counter.count, 0);
  }

  {
    Buffer::OwnedImpl buffer;
    GrpcMessageCounter counter;
    Buffer::addSeq(buffer, {0});
    EXPECT_EQ(1, IncrementMessageCounter(buffer, &counter));
    EXPECT_EQ(counter.state, GrpcMessageCounter::GrpcReadState::ExpectByte1);
    EXPECT_EQ(counter.count, 1);
  }

  {
    Buffer::OwnedImpl buffer;
    GrpcMessageCounter counter;
    Buffer::addSeq(buffer, {1, 0, 0, 0, 1, 0xFF});
    EXPECT_EQ(1, IncrementMessageCounter(buffer, &counter));
    EXPECT_EQ(counter.state, GrpcMessageCounter::GrpcReadState::ExpectByte0);
    EXPECT_EQ(counter.count, 1);
  }

  {
    GrpcMessageCounter counter;
    Buffer::OwnedImpl buffer1;
    Buffer::addSeq(buffer1, {1, 0, 0, 0});
    EXPECT_EQ(1, IncrementMessageCounter(buffer1, &counter));
    EXPECT_EQ(counter.state, GrpcMessageCounter::GrpcReadState::ExpectByte4);
    EXPECT_EQ(counter.count, 1);
    Buffer::OwnedImpl buffer2;
    Buffer::addSeq(buffer2, {1, 0xFF});
    EXPECT_EQ(0, IncrementMessageCounter(buffer2, &counter));
    EXPECT_EQ(counter.count, 1);
  }

  {
    Buffer::OwnedImpl buffer;
    GrpcMessageCounter counter;
    Buffer::addSeq(buffer, {1, 0, 0, 0, 1, 0xFF});
    Buffer::addSeq(buffer, {0, 0, 0, 0, 2, 0xFF, 0xFF});
    EXPECT_EQ(2, IncrementMessageCounter(buffer, &counter));
    EXPECT_EQ(counter.state, GrpcMessageCounter::GrpcReadState::ExpectByte0);
    EXPECT_EQ(counter.count, 2);
  }

  {
    Buffer::OwnedImpl buffer1;
    Buffer::OwnedImpl buffer2;
    GrpcMessageCounter counter;
    // message spans two buffers
    Buffer::addSeq(buffer1, {1, 0, 0, 0, 2, 0xFF});
    Buffer::addSeq(buffer2, {0xFF, 0, 0, 0, 0, 2, 0xFF, 0xFF});
    EXPECT_EQ(1, IncrementMessageCounter(buffer1, &counter));
    EXPECT_EQ(1, IncrementMessageCounter(buffer2, &counter));
    EXPECT_EQ(counter.state, GrpcMessageCounter::GrpcReadState::ExpectByte0);
    EXPECT_EQ(counter.count, 2);
  }

  {
    Buffer::OwnedImpl buffer;
    GrpcMessageCounter counter;
    // Add longer byte sequence
    Buffer::addSeq(buffer, {1, 0, 0, 1, 0});
    Buffer::addRepeated(buffer, 1 << 8, 0xFF);
    // Start second message
    Buffer::addSeq(buffer, {0});
    EXPECT_EQ(2, IncrementMessageCounter(buffer, &counter));
    EXPECT_EQ(counter.state, GrpcMessageCounter::GrpcReadState::ExpectByte1);
    EXPECT_EQ(counter.count, 2);
  }

  {
    // two empty messages
    Buffer::OwnedImpl buffer;
    GrpcMessageCounter counter;
    Buffer::addRepeated(buffer, 10, 0);
    EXPECT_EQ(2, IncrementMessageCounter(buffer, &counter));
    EXPECT_EQ(counter.count, 2);
  }
}

} // namespace
} // namespace GrpcStreaming
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
