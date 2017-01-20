#include "common/buffer/buffer_impl.h"

#include "common/common/base64.h"

TEST(Base64, EmptyBufferEncode) {
  {
    Buffer::OwnedImpl buffer;
    EXPECT_EQ("", Base64::encode(buffer, 0));
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.add("\0\0");
    EXPECT_EQ("", Base64::encode(buffer, 2));
  }
}

TEST(Base64, SingleSliceBufferEncode) {
  Buffer::OwnedImpl buffer;
  buffer.add("foo", 3);
  EXPECT_EQ("Zm9v", Base64::encode(buffer, 3));
  EXPECT_EQ("Zm8=", Base64::encode(buffer, 2));
}

TEST(Base64, MultiSlicesBufferEncode) {
  Buffer::OwnedImpl buffer;
  buffer.add("foob", 4);
  buffer.add("ar", 2);
  EXPECT_EQ("Zm9vYg==", Base64::encode(buffer, 4));
  EXPECT_EQ("Zm9vYmE=", Base64::encode(buffer, 5));
  EXPECT_EQ("Zm9vYmFy", Base64::encode(buffer, 6));
  EXPECT_EQ("Zm9vYmFy", Base64::encode(buffer, 7));
}

TEST(Base64, BinaryBufferEncode) {
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
