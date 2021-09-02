#include <limits>

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/buffer_helper.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(BufferHelperTest, DrainDouble) {
  Buffer::OwnedImpl buffer;

  // c.f. https://en.wikipedia.org/wiki/Double-precision_floating-point_format
  // 01000000 00001000 00000000 0000000 00000000 00000000 00000000 000000000 = 3
  addSeq(buffer, {0x40, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // 11111111 11101111 11111111 1111111 11111111 11111111 11111111 111111111 = -DBL_MAX
  addSeq(buffer, {0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

  EXPECT_EQ(BufferHelper::drainBEDouble(buffer), 3.0);
  EXPECT_EQ(BufferHelper::drainBEDouble(buffer), std::numeric_limits<double>::lowest());
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, PeekVarInt32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeByte(0);
    buffer.writeByte(0x7F);
    addSeq(buffer, {0xFF, 0x01});                   // 0xFF
    addSeq(buffer, {0xFF, 0xFF, 0x03});             // 0xFFFF
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0x07});       // 0xFFFFFF
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x07}); // 0x7FFFFFFF
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x0F}); // 0xFFFFFFFF

    int size = 0;
    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 0, size), 0);
    EXPECT_EQ(size, 1);

    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 1, size), 0x7F);
    EXPECT_EQ(size, 1);

    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 2, size), 0xFF);
    EXPECT_EQ(size, 2);

    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 4, size), 0xFFFF);
    EXPECT_EQ(size, 3);

    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 7, size), 0xFFFFFF);
    EXPECT_EQ(size, 4);

    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 11, size), 0x7FFFFFFF);
    EXPECT_EQ(size, 5);

    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 16, size), 0xFFFFFFFF);
    EXPECT_EQ(size, 5);
  }

  {
    Buffer::OwnedImpl buffer;
    int size = 0;
    EXPECT_THROW_WITH_MESSAGE(BufferHelper::peekVarIntI32(buffer, 0, size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    int size = 0;
    buffer.writeByte(0);
    EXPECT_THROW_WITH_MESSAGE(BufferHelper::peekVarIntI32(buffer, 1, size), EnvoyException,
                              "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekVarInt32BufferUnderflow) {
  Buffer::OwnedImpl buffer;
  int size = 0;

  for (int i = 1; i < 5; i++) {
    buffer.writeByte(0x80);
    EXPECT_EQ(BufferHelper::peekVarIntI32(buffer, 0, size), 0);
    EXPECT_EQ(size, -i);
  }

  buffer.writeByte(0x80);
  EXPECT_THROW_WITH_MESSAGE(BufferHelper::peekVarIntI32(buffer, 0, size), EnvoyException,
                            "invalid compact protocol varint i32");
}

TEST(BufferHelperTest, PeekZigZagI32) {
  Buffer::OwnedImpl buffer;
  buffer.writeByte(0);                            // unzigzag(0) = 0
  buffer.writeByte(1);                            // unzigzag(1) = -1
  buffer.writeByte(2);                            // unzigzag(2) = 1
  addSeq(buffer, {0xFE, 0x01});                   // unzigzag(0xFE) = 127
  addSeq(buffer, {0xFF, 0x01});                   // unzigzag(0xFF) = -128
  addSeq(buffer, {0xFF, 0xFF, 0x03});             // unzigzag(0xFFFF) = -32768
  addSeq(buffer, {0xFF, 0xFF, 0xFF, 0x07});       // unzigzag(0xFFFFFF) = -8388608
  addSeq(buffer, {0xFE, 0xFF, 0xFF, 0xFF, 0x07}); // unzigzag(0x7FFFFFFE) = 0x3FFFFFFF
  addSeq(buffer, {0xFE, 0xFF, 0xFF, 0xFF, 0x0F}); // unzigzag(0xFFFFFFFE) = 0x7FFFFFFF
  addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x0F}); // unzigzag(0xFFFFFFFF) = 0x80000000

  int size = 0;
  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 0, size), 0);
  EXPECT_EQ(size, 1);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 1, size), -1);
  EXPECT_EQ(size, 1);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 2, size), 1);
  EXPECT_EQ(size, 1);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 3, size), 127);
  EXPECT_EQ(size, 2);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 5, size), -128);
  EXPECT_EQ(size, 2);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 7, size), -32768);
  EXPECT_EQ(size, 3);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 10, size), -8388608);
  EXPECT_EQ(size, 4);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 14, size), 0x3FFFFFFF);
  EXPECT_EQ(size, 5);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 19, size), 0x7FFFFFFF);
  EXPECT_EQ(size, 5);

  EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 24, size), 0x80000000);
  EXPECT_EQ(size, 5);
}

TEST(BufferHelperTest, PeekZigZagI32BufferUnderflow) {
  Buffer::OwnedImpl buffer;
  int size = 0;

  for (int i = 1; i < 5; i++) {
    buffer.writeByte(0x80);
    EXPECT_EQ(BufferHelper::peekZigZagI32(buffer, 0, size), 0);
    EXPECT_EQ(size, -i);
  }

  buffer.writeByte(0x80);
  EXPECT_THROW_WITH_MESSAGE(BufferHelper::peekZigZagI32(buffer, 0, size), EnvoyException,
                            "invalid compact protocol zig-zag i32");
}

TEST(BufferHelperTest, PeekZigZagI64) {
  Buffer::OwnedImpl buffer;
  buffer.writeByte(0);                            // unzigzag(0) = 0
  buffer.writeByte(1);                            // unzigzag(1) = -1
  buffer.writeByte(2);                            // unzigzag(2) = 1
  addSeq(buffer, {0xFF, 0xFF, 0x03});             // unzigzag(0xFFFF) = -32768
  addSeq(buffer, {0xFE, 0xFF, 0xFF, 0xFF, 0x0F}); // unzigzag(0xFFFFFFFE) = 0x7FFFFFFF

  // unzigzag(0xFFFF FFFFFFFE) = 0x7FFF FFFFFFFF
  addSeq(buffer, {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F});

  // unzigzag(0x7FFFFFFF FFFFFFFE) = 0x3FFFFFFF FFFFFFFF
  addSeq(buffer, {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F});

  // unzigzag(0xFFFFFFFF FFFFFFFF) = 0x80000000 00000000 (-2^63)
  addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01});

  int size = 0;
  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 0, size), 0);
  EXPECT_EQ(size, 1);

  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 1, size), -1);
  EXPECT_EQ(size, 1);

  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 2, size), 1);
  EXPECT_EQ(size, 1);

  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 3, size), -32768);
  EXPECT_EQ(size, 3);

  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 6, size), 0x7FFFFFFF);
  EXPECT_EQ(size, 5);

  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 11, size), 0x7FFFFFFFFFFF);
  EXPECT_EQ(size, 7);

  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 18, size), 0x3FFFFFFFFFFFFFFF);
  EXPECT_EQ(size, 9);

  EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 27, size), 0x8000000000000000);
  EXPECT_EQ(size, 10);
}

TEST(BufferHelperTest, PeekZigZagI64BufferUnderflow) {
  Buffer::OwnedImpl buffer;
  int size = 0;

  for (int i = 1; i < 10; i++) {
    buffer.writeByte(0x80);
    EXPECT_EQ(BufferHelper::peekZigZagI64(buffer, 0, size), 0);
    EXPECT_EQ(size, -i);
  }

  buffer.writeByte(0x80);
  EXPECT_THROW_WITH_MESSAGE(BufferHelper::peekZigZagI64(buffer, 0, size), EnvoyException,
                            "invalid compact protocol zig-zag i64");
}

TEST(BufferHelperTest, WriteDouble) {
  // See the DrainDouble test.
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeBEDouble(buffer, 3.0);
    EXPECT_EQ(std::string("\x40\x8\0\0\0\0\0\0", 8), buffer.toString());
  }

  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeBEDouble(buffer, std::numeric_limits<double>::lowest());
    EXPECT_EQ("\xFF\xEF\xFF\xFF\xFF\xFF\xFF\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteVarIntI32) {
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, 0);
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, 1);
    EXPECT_EQ("\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, 128);
    EXPECT_EQ("\x80\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, (1 << 14) + 1);
    EXPECT_EQ("\x81\x80\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, (1 << 28) + 1);
    EXPECT_EQ("\x81\x80\x80\x80\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\x7", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, -1);
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xF", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI32(buffer, std::numeric_limits<int32_t>::min());
    EXPECT_EQ("\x80\x80\x80\x80\x8", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteVarIntI64) {
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, 0);
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, 1);
    EXPECT_EQ("\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, 128);
    EXPECT_EQ("\x80\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, (1 << 14) + 1);
    EXPECT_EQ("\x81\x80\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, (1 << 28) + 1);
    EXPECT_EQ("\x81\x80\x80\x80\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, (static_cast<int64_t>(1) << 56) + 1);
    EXPECT_EQ("\x81\x80\x80\x80\x80\x80\x80\x80\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\x7", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, std::numeric_limits<int64_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, -1);
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, std::numeric_limits<int32_t>::min());
    EXPECT_EQ("\x80\x80\x80\x80\xF8\xFF\xFF\xFF\xFF\x1", buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeVarIntI64(buffer, std::numeric_limits<int64_t>::min());
    EXPECT_EQ("\x80\x80\x80\x80\x80\x80\x80\x80\x80\x1", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteZigZagI32) {
  // zigzag(0) = 0
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, 0);
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }

  // zigzag(-1) = 1
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, -1);
    EXPECT_EQ("\x1", buffer.toString());
  }

  // zigzag(1) = 2
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, 1);
    EXPECT_EQ("\x2", buffer.toString());
  }

  // zigzag(127) = 0xFE
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, 127);
    EXPECT_EQ("\xFE\x1", buffer.toString());
  }

  // zigzag(128) = 0x100
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, 128);
    EXPECT_EQ("\x80\x2", buffer.toString());
  }

  // zigzag(-128) = 0xFF
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, -128);
    EXPECT_EQ("\xFF\x1", buffer.toString());
  }

  // zigzag(0x7FFFFFFF) = 0xFFFFFFFE
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\xFE\xFF\xFF\xFF\xF", buffer.toString());
  }

  // zigzag(0x80000000) = 0xFFFFFFFF
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI32(buffer, std::numeric_limits<int32_t>::min());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteZigZagI64) {
  // zigzag(0) = 0
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, 0);
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }

  // zigzag(-1) = 1
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, -1);
    EXPECT_EQ("\x1", buffer.toString());
  }

  // zigzag(1) = 2
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, 1);
    EXPECT_EQ("\x2", buffer.toString());
  }

  // zigzag(127) = 0xFE
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, 127);
    EXPECT_EQ("\xFE\x1", buffer.toString());
  }

  // zigzag(128) = 0x100
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, 128);
    EXPECT_EQ("\x80\x2", buffer.toString());
  }

  // zigzag(-128) = 0xFF
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, -128);
    EXPECT_EQ("\xFF\x1", buffer.toString());
  }

  // zigzag(0x7FFFFFFF) = 0xFFFFFFFE
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\xFE\xFF\xFF\xFF\xF", buffer.toString());
  }

  // zigzag(0x80000000) = 0xFFFFFFFF
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, std::numeric_limits<int32_t>::min());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xF", buffer.toString());
  }

  // zigzag(0x7FFFFFFF FFFFFFFF) = 0xFFFFFFFFFFFFFFFE
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, std::numeric_limits<int64_t>::max());
    EXPECT_EQ("\xFE\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1", buffer.toString());
  }

  // zigzag(0x8000000000000000) = 0xFFFFFFFFFFFFFFFF
  {
    Buffer::OwnedImpl buffer;
    BufferHelper::writeZigZagI64(buffer, std::numeric_limits<int64_t>::min());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1", buffer.toString());
  }
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
