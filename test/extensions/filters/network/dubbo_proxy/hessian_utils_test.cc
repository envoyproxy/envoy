#include "envoy/common/exception.h"

#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(HessianUtilsTest, peekString) {
  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x02, 't'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekString(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x30}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekString(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x30, 't'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekString(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x53, 't'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekString(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x53, 't', 'e'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekString(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x52, 't'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekString(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x20, 't'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekString(buffer, &size), EnvoyException,
                              "hessian type is not string 32");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01, 't'}));
    size_t size;
    EXPECT_STREQ("t", HessianUtils::peekString(buffer, &size).c_str());
    EXPECT_EQ(2, size);
  }

  // empty string
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x00}));
    size_t size;
    EXPECT_STREQ("", HessianUtils::peekString(buffer, &size).c_str());
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01, 0x00}));
    size_t size;
    EXPECT_STREQ("", HessianUtils::peekString(buffer, &size, 1).c_str());
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x53, 0x00, 0x05, 'h', 'e', 'l', 'l', 'o'}));
    size_t size;
    EXPECT_STREQ("hello", HessianUtils::peekString(buffer, &size).c_str());
    EXPECT_EQ(8, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string(
        {0x52, 0x00, 0x07, 'h', 'e', 'l', 'l', 'o', ',', ' ', 0x05, 'w', 'o', 'r', 'l', 'd'}));
    size_t size;
    EXPECT_STREQ("hello, world", HessianUtils::peekString(buffer, &size).c_str());
    EXPECT_EQ(16, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x31, 0x01}) + std::string(256 + 0x01, 't'));
    size_t size;
    EXPECT_STREQ(std::string(256 + 0x01, 't').c_str(),
                 HessianUtils::peekString(buffer, &size).c_str());
    EXPECT_EQ(256 + 0x01 + 2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x31, 0x01}) + std::string(256 + 0x01, 't'));
    EXPECT_STREQ(std::string(256 + 0x01, 't').c_str(), HessianUtils::readString(buffer).c_str());
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, peekLong) {
  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xf0'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekLong(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x38, '1'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekLong(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x59, '1'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekLong(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4c, '1'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekLong(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x40}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekLong(buffer, &size), EnvoyException,
                              "hessian type is not long 64");
  }

  // Single octet longs
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xef'}));
    size_t size;
    EXPECT_EQ(15, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xe0'}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xd8'}));
    size_t size;
    EXPECT_EQ(-8, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(1, size);
  }

  // Two octet longs
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xf8', 0x00}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xf0', 0x00}));
    size_t size;
    EXPECT_EQ(-2048, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xf7', 0x00}));
    size_t size;
    EXPECT_EQ(-256, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xff', '\xff'}));
    size_t size;
    EXPECT_EQ(2047, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(2, size);
  }

  // Three octet longs
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x3c, 0x00, 0x00}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(3, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x38, 0x00, 0x00}));
    size_t size;
    EXPECT_EQ(-262144, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(3, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\x3f', '\xff', '\xff'}));
    size_t size;
    EXPECT_EQ(262143, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(3, size);
  }

  // four octet longs
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x59, 0x00, 0x00, 0x00, 0x00}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(5, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01, 0x59, 0x00, 0x00, 0x01, 0x2c}));
    size_t size;
    EXPECT_EQ(300, HessianUtils::peekLong(buffer, &size, 1));
    EXPECT_EQ(5, size);
  }

  // eight octet longs
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2c}));
    size_t size;
    EXPECT_EQ(300, HessianUtils::peekLong(buffer, &size));
    EXPECT_EQ(9, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x2c}));
    EXPECT_EQ(300, HessianUtils::readLong(buffer));
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, peekBool) {
  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekBool(buffer, &size), EnvoyException,
                              "hessian type is not bool 1");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'T'}));
    size_t size;
    EXPECT_TRUE(HessianUtils::peekBool(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'F'}));
    size_t size;
    EXPECT_FALSE(HessianUtils::peekBool(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'F'}));
    EXPECT_FALSE(HessianUtils::readBool(buffer));
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, peekInt) {
  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xc1'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekInt(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xd0'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekInt(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x49}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekInt(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekInt(buffer, &size), EnvoyException,
                              "hessian type is not int 1");
  }

  // Single octet integers
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\x90'}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\x80'}));
    size_t size;
    EXPECT_EQ(-16, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xbf'}));
    size_t size;
    EXPECT_EQ(47, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(1, size);
  }

  // Two octet integers
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xc8', 0x00}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xc0', 0x00}));
    size_t size;
    EXPECT_EQ(-2048, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xc7', 0x00}));
    size_t size;
    EXPECT_EQ(-256, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xcf', '\xff'}));
    size_t size;
    EXPECT_EQ(2047, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(2, size);
  }

  // Three octet integers
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xd4', 0x00, 0x00}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(3, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xd0', 0x00, 0x00}));
    size_t size;
    EXPECT_EQ(-262144, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(3, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({'\xd7', '\xff', '\xff'}));
    size_t size;
    EXPECT_EQ(262143, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(3, size);
  }

  // Four octet integers
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x49, 0x00, 0x00, 0x00, 0x00}));
    size_t size;
    EXPECT_EQ(0, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(5, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x49, 0x00, 0x00, 0x01, 0x2c}));
    size_t size;
    EXPECT_EQ(300, HessianUtils::peekInt(buffer, &size));
    EXPECT_EQ(5, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x49, 0x00, 0x00, 0x01, 0x2c}));
    EXPECT_EQ(300, HessianUtils::readInt(buffer));
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, peekDouble) {
  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5d}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDouble(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5e}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDouble(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5f}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDouble(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x44}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDouble(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDouble(buffer, &size), EnvoyException,
                              "hessian type is not double 1");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5b}));
    size_t size;
    EXPECT_DOUBLE_EQ(0.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5c}));
    size_t size;
    EXPECT_DOUBLE_EQ(1.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5d, 0x00}));
    size_t size;
    EXPECT_DOUBLE_EQ(0.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5d, '\x80'}));
    size_t size;
    EXPECT_DOUBLE_EQ(-128.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5d, '\x7f'}));
    size_t size;
    EXPECT_DOUBLE_EQ(127.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(2, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5e, 0x00, 0x00}));
    size_t size;
    EXPECT_DOUBLE_EQ(0.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(3, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5e, '\x80', 0x00}));
    size_t size;
    EXPECT_DOUBLE_EQ(-32768.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(3, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5e, '\x7f', '\xff'}));
    size_t size;
    EXPECT_DOUBLE_EQ(32767.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(3, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5f, 0x00, 0x00, 0x00, 0x00}));
    size_t size;
    EXPECT_DOUBLE_EQ(0.0, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(5, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x44, 0x40, 0x28, '\x80', 0x00, 0x00, 0x00, 0x00, 0x00}));
    size_t size;
    EXPECT_DOUBLE_EQ(12.25, HessianUtils::peekDouble(buffer, &size));
    EXPECT_EQ(9, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x5f, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_DOUBLE_EQ(0.0, HessianUtils::readDouble(buffer));
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, peekNull) {
  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekNull(buffer, &size), EnvoyException,
                              "hessian type is not null 1");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4e}));
    size_t size = 0;
    HessianUtils::peekNull(buffer, &size);
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4e}));
    HessianUtils::readNull(buffer);
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, peekDate) {
  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4a}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDate(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4b}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDate(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekDate(buffer, &size), EnvoyException,
                              "hessian type is not date 1");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4b, 0x00, 0x00, 0x00, 0x00}));
    size_t size;
    auto t = HessianUtils::peekDate(buffer, &size);
    EXPECT_EQ(5, size);
    EXPECT_EQ(0, t.count());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4b, 0x00, '\xe3', '\x83', '\x8f'}));
    size_t size;
    auto t = HessianUtils::peekDate(buffer, &size);
    EXPECT_EQ(5, size);
    EXPECT_EQ(894621060000, t.count());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4a, 0x00, 0x00, 0x00, '\xd0', 0x4b, '\x92', '\x84', '\xb8'}));
    size_t size = 0;
    auto t = HessianUtils::peekDate(buffer, &size);
    EXPECT_EQ(9, size);
    // Time zone was UTC
    EXPECT_EQ(894621091000, t.count());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x4a, 0x00, 0x00, 0x00, '\xd0', 0x4b, '\x92', '\x84', '\xb8'}));
    auto t = HessianUtils::readDate(buffer);
    // Time zone was UTC
    EXPECT_EQ(894621091000, t.count());
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, peekByte) {
  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x23}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekByte(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x42}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekByte(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x42, 't', 'e'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekByte(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x41, 't'}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekByte(buffer, &size), EnvoyException,
                              "buffer underflow");
  }

  // Incorrect type
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x01}));
    size_t size;
    EXPECT_THROW_WITH_MESSAGE(HessianUtils::peekByte(buffer, &size), EnvoyException,
                              "hessian type is not byte 1");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x20}));
    size_t size = 0;
    EXPECT_STREQ("", HessianUtils::peekByte(buffer, &size).c_str());
    EXPECT_EQ(1, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x23, 0x01, 0x02, 0x03}));
    size_t size = 0;
    EXPECT_STREQ("\x1\x2\x3", HessianUtils::peekByte(buffer, &size).c_str());
    EXPECT_EQ(4, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x42, 0x10, 0x00}) + std::string(0x10 * 256, 't'));
    size_t size = 0;
    EXPECT_STREQ(std::string(0x10 * 256, 't').c_str(),
                 HessianUtils::peekByte(buffer, &size).c_str());
    EXPECT_EQ(3 + 0x10 * 256, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x41, 0x04, 0x00}) + std::string(0x04 * 256, 't') +
               std::string({0x23, 0x01, 0x02, 0x03}));
    size_t size = 0;
    std::string expect_string = std::string(0x04 * 256, 't') + "\x1\x2\x3";
    EXPECT_STREQ(expect_string.c_str(), HessianUtils::peekByte(buffer, &size).c_str());
    EXPECT_EQ(3 + 0x04 * 256 + 4, size);
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string({0x23, 0x01, 0x02, 0x03}));
    EXPECT_STREQ("\x1\x2\x3", HessianUtils::readByte(buffer).c_str());
    EXPECT_EQ(0, buffer.length());
  }
}

TEST(HessianUtilsTest, writeString) {
  const size_t max = 65535;
  const size_t segment_mark_length = 3;

  {
    const std::string append_content("b");
    const size_t append_content_hessian_length = 1;
    std::string message(max, 'a');
    message.append(append_content);
    size_t expect_serialized_size =
        max + segment_mark_length + append_content_hessian_length + append_content.size();

    Buffer::OwnedImpl buffer;
    size_t size = HessianUtils::writeString(buffer, message);
    EXPECT_EQ(size, expect_serialized_size);
  }

  {
    const std::string append_content(33, 'b');
    const size_t append_content_hessian_length = 2;
    std::string message(max, 'a');
    message.append(append_content);
    size_t expect_serialized_size =
        max + segment_mark_length + append_content_hessian_length + append_content.size();

    Buffer::OwnedImpl buffer;
    size_t size = HessianUtils::writeString(buffer, message);
    EXPECT_EQ(size, expect_serialized_size);
  }

  {
    const std::string append_content(1025, 'b');
    const size_t append_content_hessian_length = 3;
    std::string message(max, 'a');
    message.append(append_content);
    size_t expect_serialized_size =
        max + segment_mark_length + append_content_hessian_length + append_content.size();

    Buffer::OwnedImpl buffer;
    size_t size = HessianUtils::writeString(buffer, message);
    EXPECT_EQ(size, expect_serialized_size);
  }

  {
    const std::string append_content(1025, 'b');
    const size_t append_content_hessian_length = 3;
    const size_t max_size = 2 * max;
    std::string message(max_size, 'a');
    message.append(append_content);
    size_t expect_serialized_size =
        max * 2 + segment_mark_length * 2 + append_content_hessian_length + append_content.size();

    Buffer::OwnedImpl buffer;
    size_t size = HessianUtils::writeString(buffer, message);
    EXPECT_EQ(size, expect_serialized_size);
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
