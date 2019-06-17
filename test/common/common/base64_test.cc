#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/common/base64.h"

#include "test/test_common/printers.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Base64Test, EmptyBufferEncode) {
  {
    Buffer::OwnedImpl buffer;
    EXPECT_EQ("", Base64::encode(buffer, 0));
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add("\0\0", 2);
    EXPECT_EQ("AAA=", Base64::encode(buffer, 2));
  }
}

TEST(Base64Test, SingleSliceBufferEncode) {
  Buffer::OwnedImpl buffer;
  buffer.add("foo", 3);
  EXPECT_EQ("Zm9v", Base64::encode(buffer, 3));
  EXPECT_EQ("Zm8=", Base64::encode(buffer, 2));
}

TEST(Base64Test, EncodeString) {
  EXPECT_EQ("", Base64::encode("", 0));
  EXPECT_EQ("AAA=", Base64::encode("\0\0", 2));
  EXPECT_EQ("AAA", Base64::encode("\0\0", 2, false));
  EXPECT_EQ("Zm9v", Base64::encode("foo", 3));
  EXPECT_EQ("Zm8=", Base64::encode("fo", 2));
  EXPECT_EQ("Zg==", Base64::encode("f", 1));
  EXPECT_EQ("Zg", Base64::encode("f", 1, false));
}

TEST(Base64Test, Decode) {
  EXPECT_EQ("", Base64::decode(""));
  EXPECT_EQ("foo", Base64::decode("Zm9v"));
  EXPECT_EQ("fo", Base64::decode("Zm8="));
  EXPECT_EQ("f", Base64::decode("Zg=="));
  EXPECT_EQ("foobar", Base64::decode("Zm9vYmFy"));
  EXPECT_EQ("foob", Base64::decode("Zm9vYg=="));

  {
    const char* test_string = "\0\1\2\3\b\n\t";
    EXPECT_FALSE(memcmp(test_string, Base64::decode("AAECAwgKCQ==").data(), 7));
  }

  {
    const char* test_string = "\0\0\0\0als;jkopqitu[\0opbjlcxnb35g]b[\xaa\b\n";
    Buffer::OwnedImpl buffer;
    buffer.add(test_string, 36);
    EXPECT_FALSE(memcmp(test_string, Base64::decode(Base64::encode(buffer, 36)).data(), 36));
  }

  {
    const char* test_string = "\0\0\0\0als;jkopqitu[\0opbjlcxnb35g]b[\xaa\b\n";
    EXPECT_FALSE(memcmp(test_string, Base64::decode(Base64::encode(test_string, 36)).data(), 36));
  }

  {
    std::string test_string = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded = Base64::decode(test_string);
    Buffer::OwnedImpl buffer(decoded);
    EXPECT_EQ(test_string, Base64::encode(buffer, decoded.length()));
  }

  {
    const char* test_string = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded = Base64::decode(test_string);
    EXPECT_EQ(test_string, Base64::encode(decoded.c_str(), decoded.length()));
  }
}

TEST(Base64Test, DecodeFailure) {
  EXPECT_EQ("", Base64::decode("==Zg"));
  EXPECT_EQ("", Base64::decode("=Zm8"));
  EXPECT_EQ("", Base64::decode("Zm=8"));
  EXPECT_EQ("", Base64::decode("Zg=A"));
  EXPECT_EQ("", Base64::decode("Zh==")); // 011001 100001 <- unused bit at tail
  EXPECT_EQ("", Base64::decode("Zm9=")); // 011001 100110 111101 <- unused bit at tail
  EXPECT_EQ("", Base64::decode("Zg.."));
  EXPECT_EQ("", Base64::decode("..Zg"));
  EXPECT_EQ("", Base64::decode("A==="));
  EXPECT_EQ("", Base64::decode("===="));
  EXPECT_EQ("", Base64::decode("123"));
}

TEST(Base64Test, DecodeWithoutPadding) {
  EXPECT_EQ("foo", Base64::decodeWithoutPadding("Zm9v"));
  EXPECT_EQ("fo", Base64::decodeWithoutPadding("Zm8"));
  EXPECT_EQ("f", Base64::decodeWithoutPadding("Zg"));
  EXPECT_EQ("foobar", Base64::decodeWithoutPadding("Zm9vYmFy"));
  EXPECT_EQ("fooba", Base64::decodeWithoutPadding("Zm9vYmE"));
  EXPECT_EQ("foob", Base64::decodeWithoutPadding("Zm9vYg"));

  EXPECT_EQ("", Base64::decodeWithoutPadding(""));
  EXPECT_EQ("", Base64::decodeWithoutPadding("="));
  EXPECT_EQ("", Base64::decodeWithoutPadding("=="));
  EXPECT_EQ("", Base64::decodeWithoutPadding("==="));
  EXPECT_EQ("", Base64::decodeWithoutPadding("===="));

  EXPECT_EQ("f", Base64::decodeWithoutPadding("Zg"));
  EXPECT_EQ("f", Base64::decodeWithoutPadding("Zg="));
  EXPECT_EQ("f", Base64::decodeWithoutPadding("Zg=="));
}

TEST(Base64Test, MultiSlicesBufferEncode) {
  Buffer::OwnedImpl buffer;
  buffer.add("foob", 4);
  buffer.add("ar", 2);
  EXPECT_EQ("Zm9vYg==", Base64::encode(buffer, 4));
  EXPECT_EQ("Zm9vYmE=", Base64::encode(buffer, 5));
  EXPECT_EQ("Zm9vYmFy", Base64::encode(buffer, 6));
  EXPECT_EQ("Zm9vYmFy", Base64::encode(buffer, 7));
}

TEST(Base64Test, BinaryBufferEncode) {
  Buffer::OwnedImpl buffer;
  buffer.add("\0\1\2\3", 4);
  buffer.add("\b\n\t", 4);
  buffer.add("\xaa\xbc\xde", 3);
  EXPECT_EQ("AAECAwgKCQ==", Base64::encode(buffer, 7));
  EXPECT_EQ("AAECAwgKCQA=", Base64::encode(buffer, 8));
  EXPECT_EQ("AAECAwgKCQCq", Base64::encode(buffer, 9));
  EXPECT_EQ("AAECAwgKCQCqvA==", Base64::encode(buffer, 10));
  EXPECT_EQ("AAECAwgKCQCqvN4=", Base64::encode(buffer, 30));
}

TEST(Base64UrlTest, EncodeString) {
  EXPECT_EQ("", Base64Url::encode("", 0));
  EXPECT_EQ("AAA", Base64Url::encode("\0\0", 2));
  EXPECT_EQ("Zm9v", Base64Url::encode("foo", 3));
  EXPECT_EQ("Zm8", Base64Url::encode("fo", 2));
}

TEST(Base64UrlTest, Decode) {
  EXPECT_EQ("", Base64Url::decode(""));
  EXPECT_EQ("foo", Base64Url::decode("Zm9v"));
  EXPECT_EQ("fo", Base64Url::decode("Zm8"));
  EXPECT_EQ("f", Base64Url::decode("Zg"));
  EXPECT_EQ("foobar", Base64Url::decode("Zm9vYmFy"));
  EXPECT_EQ("foob", Base64Url::decode("Zm9vYg"));

  {
    const char* test_string = "\0\1\2\3\b\n\t";
    EXPECT_FALSE(memcmp(test_string, Base64Url::decode("AAECAwgKCQ").data(), 7));
  }

  {
    const char* test_string = "\0\0\0\0als;jkopqitu[\0opbjlcxnb35g]b[\xaa\b\n";
    EXPECT_FALSE(
        memcmp(test_string, Base64Url::decode(Base64Url::encode(test_string, 36)).data(), 36));
  }

  {
    const char* test_string = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string decoded = Base64Url::decode(test_string);
    EXPECT_EQ(test_string, Base64Url::encode(decoded.c_str(), decoded.length()));
  }

  {
    const char* url_test_string =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const char* test_string = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    EXPECT_EQ(Base64Url::decode(url_test_string), Base64::decode(test_string));
  }
}

TEST(Base64UrlTest, DecodeFailure) {
  EXPECT_EQ("", Base64Url::decode("==Zg"));
  EXPECT_EQ("", Base64Url::decode("=Zm8"));
  EXPECT_EQ("", Base64Url::decode("Zm=8"));
  EXPECT_EQ("", Base64Url::decode("Zg=A"));
  EXPECT_EQ("", Base64Url::decode("Zh==")); // 011001 100001 <- unused bit at tail
  EXPECT_EQ("", Base64Url::decode("Zm9=")); // 011001 100110 111101 <- unused bit at tail
  EXPECT_EQ("", Base64Url::decode("Zg.."));
  EXPECT_EQ("", Base64Url::decode("..Zg"));
  EXPECT_EQ("", Base64Url::decode("A==="));
  EXPECT_EQ("", Base64Url::decode("Zh"));  // 011001 100001 <- unused bit at tail
  EXPECT_EQ("", Base64Url::decode("Zm9")); // 011001 100110 111101 <- unused bit at tail
  EXPECT_EQ("", Base64Url::decode("A"));
}
} // namespace Envoy
