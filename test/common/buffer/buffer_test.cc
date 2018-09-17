#include <limits>

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "test/common/buffer/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

TEST(BufferHelperTest, PeekI8) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 0xFE});
    EXPECT_EQ(buffer.peekInt<int8_t>(), 0);
    EXPECT_EQ(buffer.peekInt<int8_t>(0), 0);
    EXPECT_EQ(buffer.peekInt<int8_t>(1), 1);
    EXPECT_EQ(buffer.peekInt<int8_t>(2), -2);
    EXPECT_EQ(buffer.length(), 3);
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekInt<int8_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.writeByte(0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekInt<int8_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEI16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<int16_t>(), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(0), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(1), 0x0201);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(2), 0x0302);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(4), -1);
    EXPECT_EQ(buffer.length(), 6);
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEI32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<int32_t>(), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(0), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(1), 0xFF030201);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(2), 0xFFFF0302);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(4), -1);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEI64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<int64_t>(), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(0), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(1), 0xFF07060504030201);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(2), 0xFFFF070605040302);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(8), -1);

    EXPECT_EQ(buffer.length(), 16);

    // partial
    EXPECT_EQ((buffer.peekLEInt<int64_t, 4>()), 0x03020100);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 4>(1)), 0x04030201);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>()), 0x0100);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>(1)), 0x0201);
  }

  {
    // signed
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFE, 0xFF, 0xFF});
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>()), -1);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>(2)), 255);  // 0x00FF
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>(3)), -256); // 0xFF00
    EXPECT_EQ((buffer.peekLEInt<int64_t, 3>(5)), -2);   // 0xFFFFFE
  }

  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF});
    EXPECT_THROW_WITH_MESSAGE(
        (buffer.peekLEInt<int64_t, sizeof(int64_t)>(buffer.length() - sizeof(int64_t) + 1)),
        EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEU16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(0), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(1), 0x0201);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(2), 0x0302);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(4), 0xFFFF);
    EXPECT_EQ(buffer.length(), 6);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEU32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(0), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(1), 0xFF030201);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(2), 0xFFFF0302);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(4), 0xFFFFFFFF);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEU64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(0), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(1), 0xFF07060504030201);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(2), 0xFFFF070605040302);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(8), 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(buffer.length(), 16);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEI16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<int16_t>(), 1);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(0), 1);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(1), 0x0102);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(2), 0x0203);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(4), -1);
    EXPECT_EQ(buffer.length(), 6);
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEI32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<int32_t>(), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(0), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(1), 0x010203FF);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(2), 0x0203FFFF);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(4), -1);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEI64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<int64_t>(), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(0), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(1), 0x01020304050607FF);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(2), 0x020304050607FFFF);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(8), -1);
    EXPECT_EQ(buffer.length(), 16);

    // partial
    EXPECT_EQ((buffer.peekBEInt<int64_t, 4>()), 0x00010203);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 4>(1)), 0x01020304);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>()), 0x0001);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>(1)), 0x0102);
  }

  {
    // signed
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFE});
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>()), -1);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>(2)), -256); // 0xFF00
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>(3)), 255);  // 0x00FF
    EXPECT_EQ((buffer.peekBEInt<int64_t, 3>(5)), -2);   // 0xFFFFFE
  }

  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF});
    EXPECT_THROW_WITH_MESSAGE(
        (buffer.peekBEInt<int64_t, sizeof(int64_t)>(buffer.length() - sizeof(int64_t) + 1)),
        EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEU16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(), 1);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(0), 1);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(1), 0x0102);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(2), 0x0203);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(4), 0xFFFF);
    EXPECT_EQ(buffer.length(), 6);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEU32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(0), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(1), 0x010203FF);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(2), 0x0203FFFF);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(4), 0xFFFFFFFF);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEU64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(0), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(1), 0x01020304050607FF);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(2), 0x020304050607FFFF);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(8), 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(buffer.length(), 16);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, DrainI8) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 0xFE});
  EXPECT_EQ(buffer.drainInt<int8_t>(), 0);
  EXPECT_EQ(buffer.drainInt<int8_t>(), 1);
  EXPECT_EQ(buffer.drainInt<int8_t>(), -2);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEI16) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<int16_t>(), 0x0100);
  EXPECT_EQ(buffer.drainLEInt<int16_t>(), 0x0302);
  EXPECT_EQ(buffer.drainLEInt<int16_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEI32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<int32_t>(), 0x03020100);
  EXPECT_EQ(buffer.drainLEInt<int32_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEI64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<int64_t>(), 0x0706050403020100);
  EXPECT_EQ(buffer.drainLEInt<int64_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEU32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<uint32_t>(), 0x03020100);
  EXPECT_EQ(buffer.drainLEInt<uint32_t>(), 0xFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEU64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<uint64_t>(), 0x0706050403020100);
  EXPECT_EQ(buffer.drainLEInt<uint64_t>(), 0xFFFFFFFFFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEI16) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<int16_t>(), 1);
  EXPECT_EQ(buffer.drainBEInt<int16_t>(), 0x0203);
  EXPECT_EQ(buffer.drainBEInt<int16_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEI32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<int32_t>(), 0x00010203);
  EXPECT_EQ(buffer.drainBEInt<int32_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEI64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<int64_t>(), 0x0001020304050607);
  EXPECT_EQ(buffer.drainBEInt<int64_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEU32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<uint32_t>(), 0x00010203);
  EXPECT_EQ(buffer.drainBEInt<uint32_t>(), 0xFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEU64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<uint64_t>(), 0x0001020304050607);
  EXPECT_EQ(buffer.drainBEInt<uint64_t>(), 0xFFFFFFFFFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, WriteI8) {
  Buffer::OwnedImpl buffer;
  buffer.writeByte(-128);
  buffer.writeByte(-1);
  buffer.writeByte(0);
  buffer.writeByte(1);
  buffer.writeByte(127);

  EXPECT_EQ(std::string("\x80\xFF\0\x1\x7F", 5), buffer.toString());
}

TEST(BufferHelperTest, WriteLEI16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(std::numeric_limits<int16_t>::min());
    EXPECT_EQ(std::string("\0\x80", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(1);
    EXPECT_EQ(std::string("\x1\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(std::numeric_limits<int16_t>::max());
    EXPECT_EQ("\xFF\x7F", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteLEU16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(1);
    EXPECT_EQ(std::string("\x1\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(static_cast<uint16_t>(std::numeric_limits<int16_t>::max()) + 1);
    EXPECT_EQ(std::string("\0\x80", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(std::numeric_limits<uint16_t>::max());
    EXPECT_EQ("\xFF\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteLEI32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(std::numeric_limits<int32_t>::min());
    EXPECT_EQ(std::string("\0\0\0\x80", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(1);
    EXPECT_EQ(std::string("\x1\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\x7F", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteLEU32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(1);
    EXPECT_EQ(std::string("\x1\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) + 1);
    EXPECT_EQ(std::string("\0\0\0\x80", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(std::numeric_limits<uint32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF", buffer.toString());
  }
}
TEST(BufferHelperTest, WriteLEI64) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(std::numeric_limits<int64_t>::min());
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\x80", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(1);
    EXPECT_EQ(std::string("\x1\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(std::numeric_limits<int64_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEI16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(std::numeric_limits<int16_t>::min());
    EXPECT_EQ(std::string("\x80\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(1);
    EXPECT_EQ(std::string("\0\x1", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(std::numeric_limits<int16_t>::max());
    EXPECT_EQ("\x7F\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEU16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(1);
    EXPECT_EQ(std::string("\0\x1", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(static_cast<uint16_t>(std::numeric_limits<int16_t>::max()) + 1);
    EXPECT_EQ(std::string("\x80\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(std::numeric_limits<uint16_t>::max());
    EXPECT_EQ("\xFF\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEI32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(std::numeric_limits<int32_t>::min());
    EXPECT_EQ(std::string("\x80\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(1);
    EXPECT_EQ(std::string("\0\0\0\x1", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\x7F\xFF\xFF\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEU32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(1);
    EXPECT_EQ(std::string("\0\0\0\x1", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) + 1);
    EXPECT_EQ(std::string("\x80\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(std::numeric_limits<uint32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF", buffer.toString());
  }
}
TEST(BufferHelperTest, WriteBEI64) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(std::numeric_limits<int64_t>::min());
    EXPECT_EQ(std::string("\x80\0\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(1);
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\x1", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(std::numeric_limits<int64_t>::max());
    EXPECT_EQ("\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF", buffer.toString());
  }
}

} // namespace
} // namespace Buffer
} // namespace Envoy
