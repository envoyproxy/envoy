#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/header_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

class MockBuffer : public Envoy::MockBuffer {
public:
  MockBuffer() = default;
  ~MockBuffer() override = default;

  MOCK_METHOD(uint64_t, length, (), (const));
};

MessageMetadataSharedPtr mkMessageMetadata(uint32_t num_headers) {
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>(true);

  while (num_headers-- > 0) {
    metadata->requestHeaders().addCopy(Http::LowerCaseString("x"), "y");
  }
  return metadata;
}

} // namespace

TEST(HeaderTransportTest, Name) {
  HeaderTransportImpl transport;
  EXPECT_EQ(transport.name(), "header");
}

TEST(HeaderTransportTest, NotEnoughData) {
  HeaderTransportImpl transport;
  MessageMetadata metadata;

  // Empty buffer
  {
    Buffer::OwnedImpl buffer;
    EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  // Too short for minimum header
  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 13, 0);
    EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  // Missing header data
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(1); // header size / 4
    addRepeated(buffer, 3, 0);
    EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }
}

TEST(HeaderTransportTest, InvalidFrameSize) {
  HeaderTransportImpl transport;
  MessageMetadata metadata;

  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(-1);
    addRepeated(buffer, 10, 0);
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid thrift header transport frame size -1");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(0x7fffffff);
    addRepeated(buffer, 10, 0);

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid thrift header transport frame size 2147483647");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }
}

TEST(HeaderTransportTest, InvalidMagic) {
  HeaderTransportImpl transport;
  Buffer::OwnedImpl buffer;
  MessageMetadata metadata;

  buffer.writeBEInt<int32_t>(0x100);
  buffer.writeBEInt<int16_t>(0x0123);
  addRepeated(buffer, 8, 0);
  EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                            "invalid thrift header transport magic 0123");
  EXPECT_THAT(metadata, IsEmptyMetadata());
}

TEST(HeaderTransportTest, InvalidHeaderSize) {
  HeaderTransportImpl transport;
  MessageMetadata metadata;

  // Minimum header size is 1 = 4 bytes
  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(0x100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(0);
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "no header data");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  // Minimum header size is 1 = 4 bytes
  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(0x100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(-1);
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid thrift header transport header size -4 (ffff)");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  // Max header size is 16384 = 65536 bytes
  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(0x100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(0x4001);
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid thrift header transport header size 65540 (4001)");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  // Header data extends past stated header size.
  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(0x100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1);                  // sequence number
    buffer.writeBEInt<int16_t>(1);                  // 4 bytes
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // var int -1, exceeds header size
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "unable to read header transport protocol id: header too small");
  }

  // Partial var-int at end of header
  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(0x100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1);            // sequence number
    buffer.writeBEInt<int16_t>(1);            // 4 bytes
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF}); // partial var int
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "unable to read header transport protocol id: header too small");
  }
}

TEST(HeaderTransportTest, InvalidProto) {
  HeaderTransportImpl transport;
  MessageMetadata metadata;

  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(1); // size 4
    addSeq(buffer, {1, 0, 0, 0});  // 1 = json, 0 = num transforms, pad, pad
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "Unknown protocol 1");
  }

  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(1); // size 4
    addSeq(buffer, {3, 0, 0, 0});  // 3 = invalid proto, 0 = num transforms, pad, pad
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "Unknown protocol 3");
  }

  {
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1);                  // sequence number
    buffer.writeBEInt<int16_t>(2);                  // size 8
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // -1 = invalid proto
    addSeq(buffer, {0, 0, 0});                      // 0 transforms and padding
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "Unknown protocol -1");
  }
}

TEST(HeaderTransportTest, NoTransformsOrInfo) {
  HeaderTransportImpl transport;

  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(1); // header flags
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(1); // size 4
    addSeq(buffer, {0, 0, 0, 0});  // 0 = binary proto, 0 = num transforms, pad, pad
    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, HasFrameSize(86U));
    EXPECT_THAT(metadata, HasProtocol(ProtocolType::Binary));
    EXPECT_THAT(metadata, HasHeaderFlags(1));
    EXPECT_THAT(metadata, HasSequenceId(1));
    EXPECT_THAT(metadata, HasNoRequestHeaders());
    EXPECT_EQ(buffer.length(), 0);
  }

  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    buffer.writeBEInt<int32_t>(101);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(2); // header flags
    buffer.writeBEInt<int32_t>(2); // sequence number
    buffer.writeBEInt<int16_t>(1); // size 4
    addSeq(buffer, {2, 0, 0, 0});  // 2 = compact proto, 0 = num transforms, pad, pad
    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, HasFrameSize(87U));
    EXPECT_THAT(metadata, HasProtocol(ProtocolType::Compact));
    EXPECT_THAT(metadata, HasHeaderFlags(2));
    EXPECT_THAT(metadata, HasSequenceId(2));
    EXPECT_THAT(metadata, HasNoRequestHeaders());
  }
}

TEST(HeaderTransportTest, TransformErrors) {
  MessageMetadata metadata;

  // Invalid number of transforms
  {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1);                  // sequence number
    buffer.writeBEInt<int16_t>(2);                  // size 8
    buffer.writeByte(0);                            // binary proto
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // -1 = invalid num transforms
    addSeq(buffer, {0, 0});                         // padding

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid header transport transform count -1");
  }

  // Unknown transform ids
  for (uint8_t xform_id = 1; xform_id < 5; xform_id++) {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1);       // sequence number
    buffer.writeBEInt<int16_t>(1);       // size 4
    addSeq(buffer, {0, 1, xform_id, 0}); // 0 = binary proto, 1 = num transforms, xform id, pad

    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, HasFrameSize(86U));
    EXPECT_THAT(metadata, HasProtocol(ProtocolType::Binary));
    EXPECT_THAT(metadata, HasAppException(AppExceptionType::MissingResult,
                                          absl::StrCat("Unknown transform ", xform_id)));
  }

  // Only the first of multiple errors is reported
  {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(1); // size 4
    addSeq(buffer, {0, 2, 1, 2});  // 0 = binary proto, 2 = num transforms, xform id 1, xform id 2

    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, HasFrameSize(86U));
    EXPECT_THAT(metadata, HasProtocol(ProtocolType::Binary));
    EXPECT_THAT(metadata, HasAppException(AppExceptionType::MissingResult, "Unknown transform 1"));
  }
}

TEST(HeaderTransportTest, InvalidInfoBlock) {
  // Unknown info block id
  {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(1); // header flags
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(1); // size 4
    addSeq(buffer, {0, 0, 2, 0});  // 0 = binary proto, 0 = num transforms, 2 = unknown info id, pad

    // Unknown info id is ignored.
    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, HasFrameSize(86U));
    EXPECT_THAT(metadata, HasProtocol(ProtocolType::Binary));
    EXPECT_THAT(metadata, HasHeaderFlags(1));
    EXPECT_THAT(metadata, HasSequenceId(1));
    EXPECT_THAT(metadata, HasNoRequestHeaders());
    EXPECT_EQ(buffer.length(), 0);
  }

  // Num headers info info block id 1 must be >= 0
  {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(3); // size 12
    addSeq(buffer, {0, 0, 1});     // 0 = binary proto, 0 = num transforms, 1 key-value
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // -1 headers
    addSeq(buffer, {0, 0, 0, 0});

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid header transport header count -1");
  }

  // Header key length exceeds max allowed size
  {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(2); // size 8
    addSeq(buffer, {0, 0, 1, 1});  // 0 = binary proto, 0 = num transforms, 1 key-value, 1 = num kvs
    addSeq(buffer, {0x80, 0x80, 0x40}); // var int 0x100000
    buffer.writeByte(0);

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "header transport header key: value 1048576 exceeds max i16 (32767)");
  }

  // Header key extends past stated header size
  {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(2); // size 8
    addSeq(buffer, {0, 0, 1, 1});  // 0 = binary proto, 0 = num transforms, 1 key-value, 1 = num kvs
    buffer.writeByte(4);           // exceeds specified header size
    buffer.add("key_");

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "unable to read header transport header key: header too small");
  }

  // Header key ends at stated header size (no value)
  {
    HeaderTransportImpl transport;
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;

    buffer.writeBEInt<int32_t>(100);
    buffer.writeBEInt<int16_t>(0x0FFF);
    buffer.writeBEInt<int16_t>(0);
    buffer.writeBEInt<int32_t>(1); // sequence number
    buffer.writeBEInt<int16_t>(2); // size 8
    addSeq(buffer, {0, 0, 1, 1});  // 0 = binary proto, 0 = num transforms, 1 key-value, 1 = num kvs
    buffer.writeByte(3);           // head ends with key, no room for value
    buffer.add("abc");
    buffer.writeByte(0);

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "unable to read header transport header value: header too small");
  }
}

MessageMetadata testInfoBlock(bool preserve_keys, const std::string& key,
                              const std::string& value) {
  HeaderTransportImpl transport;
  Buffer::OwnedImpl buffer;
  MessageMetadata metadata(true, preserve_keys);

  metadata.requestHeaders().addCopy(Http::LowerCaseString("not"), "empty");

  buffer.writeBEInt<int32_t>(200);
  buffer.writeBEInt<int16_t>(0x0FFF);
  buffer.writeBEInt<int16_t>(0);
  buffer.writeBEInt<int32_t>(1);  // sequence number
  buffer.writeBEInt<int16_t>(38); // size 152
  addSeq(buffer, {0, 0, 1, 3}); // 0 = binary proto, 0 = num transforms, 1 = key value, 3 = num kvs
  buffer.writeByte(key.size());
  buffer.add(key);
  buffer.writeByte(value.size());
  buffer.add(value);
  buffer.writeByte(4);
  buffer.add("key2");
  addSeq(buffer, {0x80, 0x01}); // var int 128
  buffer.add(std::string(128, 'x'));
  buffer.writeByte(0); // empty key
  buffer.writeByte(0); // empty value
  buffer.writeByte(0); // padding

  Http::TestRequestHeaderMapImpl expected_headers;
  expected_headers.addCopy(Http::LowerCaseString("not"), "empty");
  expected_headers.addCopy(Http::LowerCaseString(absl::StrReplaceAll(
                               key, {{std::string(1, '\0'), ""}, {"\n", ""}, {"\r", ""}})),
                           value);
  expected_headers.addCopy(Http::LowerCaseString("key2"), std::string(128, 'x'));
  expected_headers.addCopy(Http::LowerCaseString(""), "");

  EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, HasFrameSize(38U));

  EXPECT_EQ(expected_headers, metadata.requestHeaders());
  EXPECT_EQ(buffer.length(), 0);

  return metadata;
}

TEST(HeaderTransportTest, InfoBlock) { testInfoBlock(false /* preserve-keys */, "key", "value"); }

TEST(HeaderTransportTest, InfoBlockCaseSensitive) {
  auto metadata = testInfoBlock(true /* preserve-keys */, "Key", "Value");
  HeaderTransportImpl transport;
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl msg;
  msg.add("fake message");
  transport.encodeFrame(buffer, metadata, msg);
  EXPECT_EQ(0, msg.length());
  EXPECT_EQ(std::string("\0\0\0\xBA\xF\xFF\0\0\0\0\0\x1\0)\0\0\x1\x4\x3not\x5"
                        "empty\x3Key\x5Value\x4key2\x80\x1xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                        "xxxxxxxxxxxxx\0\0\0\0\0fake message",
                        190),
            buffer.toString());
}

TEST(HeaderTransportTest, InfoBlockCaseSensitiveNewline) {
  auto metadata = testInfoBlock(true /* preserve-keys */, "K\ny", "Value");
  HeaderTransportImpl transport;
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl msg;
  msg.add("fake message");
  transport.encodeFrame(buffer, metadata, msg);
  EXPECT_EQ(0, msg.length());
  EXPECT_EQ(
      std::string("\0\0\0\xBA\xF\xFF\0\0\0\0\0\x1\0)\0\0\x1\x4\x3not\x5"
                  "empty\x3K\ny\x5Value\x4key2\x80\x1xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                  "xxxxxxxxxxxxx\0\0\0\0\0fake message",
                  190),
      buffer.toString());
}

TEST(HeaderTransportTest, DecodeFrameEnd) {
  HeaderTransportImpl transport;
  Buffer::OwnedImpl buffer;
  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(HeaderTransportImpl, TestEncodeFrame) {
  HeaderTransportImpl transport;

  // No message
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;
    Buffer::OwnedImpl msg;

    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, metadata, msg), EnvoyException,
                              "invalid thrift header transport message size 0");
  }

  // No protocol
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;
    Buffer::OwnedImpl msg;
    msg.add("fake message");

    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, metadata, msg), EnvoyException,
                              "missing header transport protocol");
  }

  // Illegal protocol
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;
    metadata.setProtocol(ProtocolType::Auto);
    Buffer::OwnedImpl msg;
    msg.add("fake message");

    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, metadata, msg), EnvoyException,
                              "invalid header transport protocol auto");
  }

  // Message too large
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;
    metadata.setProtocol(ProtocolType::Binary);

    MockBuffer msg;
    EXPECT_CALL(msg, length()).WillOnce(Return(0x40000000));

    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, metadata, msg), EnvoyException,
                              "invalid thrift header transport frame size 1073741838");
  }

  // Too many headers
  {
    Buffer::OwnedImpl buffer;
    MessageMetadataSharedPtr metadata = mkMessageMetadata(32769);
    metadata->setProtocol(ProtocolType::Binary);

    Buffer::OwnedImpl msg;
    msg.add("fake message");

    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, *metadata, msg), EnvoyException,
                              "invalid thrift header transport too many headers 32769");
  }

  // Header string too large
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata(true);
    metadata.setProtocol(ProtocolType::Binary);
    metadata.requestHeaders().addCopy(Http::LowerCaseString("key"), std::string(32768, 'x'));

    Buffer::OwnedImpl msg;
    msg.add("fake message");

    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, metadata, msg), EnvoyException,
                              "header string too long: 32768");
  }

  // Header info block too large
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata(true);
    metadata.setProtocol(ProtocolType::Binary);
    metadata.requestHeaders().addCopy(Http::LowerCaseString("k1"), std::string(16384, 'x'));
    metadata.requestHeaders().addCopy(Http::LowerCaseString("k2"), std::string(16384, 'x'));
    metadata.requestHeaders().addCopy(Http::LowerCaseString("k3"), std::string(16384, 'x'));
    metadata.requestHeaders().addCopy(Http::LowerCaseString("k4"), std::string(16384, 'x'));

    Buffer::OwnedImpl msg;
    msg.add("fake message");

    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, metadata, msg), EnvoyException,
                              "invalid thrift header transport header size 65568");
  }

  // Trivial frame with binary protocol
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;
    metadata.setProtocol(ProtocolType::Binary);
    Buffer::OwnedImpl msg;
    msg.add("fake message");

    transport.encodeFrame(buffer, metadata, msg);

    EXPECT_EQ(0, msg.length());
    EXPECT_EQ(std::string("\0\0\0\x1a"
                          "\xf\xff\0\0"
                          "\0\0\0\0"
                          "\0\x1"
                          "\0\0\0\0"
                          "fake message",
                          30),
              buffer.toString());
  }

  // Trivial frame with compact protocol
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata;
    metadata.setProtocol(ProtocolType::Compact);
    metadata.setSequenceId(10);
    Buffer::OwnedImpl msg;
    msg.add("fake message");

    transport.encodeFrame(buffer, metadata, msg);

    EXPECT_EQ(0, msg.length());
    EXPECT_EQ(std::string("\0\0\0\x1a"
                          "\xf\xff\0\0"
                          "\0\0\0\x0a"
                          "\0\x1"     // header size = 4
                          "\x2\0\0\0" // compact, no transforms, padding
                          "fake message",
                          30),
              buffer.toString());
  }

  // Frame with headers
  {
    Buffer::OwnedImpl buffer;
    MessageMetadata metadata(true);
    metadata.setProtocol(ProtocolType::Compact);
    metadata.setSequenceId(10);
    metadata.requestHeaders().addCopy(Http::LowerCaseString("key"), "value");
    metadata.requestHeaders().addCopy(Http::LowerCaseString(""), "");
    Buffer::OwnedImpl msg;
    msg.add("fake message");

    transport.encodeFrame(buffer, metadata, msg);

    EXPECT_EQ(0, msg.length());
    EXPECT_EQ(std::string("\0\0\0\x2a"
                          "\xf\xff\0\0"
                          "\0\0\0\x0a"
                          "\0\x5"          // header size = 20
                          "\x2\0"          // compact, no transforms
                          "\x1\x2"         // header info block, 2 headers
                          "\x3key\x5value" // first header
                          "\0\0"           // second header
                          "\0\0\0\0"       // padding
                          "fake message",
                          46),
              buffer.toString());
  }
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
