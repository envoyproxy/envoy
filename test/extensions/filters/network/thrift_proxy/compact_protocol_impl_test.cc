#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class CompactProtocolTest : public testing::Test {
public:
  void resetMetadata() {
    metadata_.setMethodName("-");
    metadata_.setMessageType(MessageType::Oneway);
    metadata_.setSequenceId(1);
  }

  void expectMetadata(const std::string& name, MessageType msg_type, int32_t seq_id) {
    EXPECT_TRUE(metadata_.hasMethodName());
    EXPECT_EQ(name, metadata_.methodName());

    EXPECT_TRUE(metadata_.hasMessageType());
    EXPECT_EQ(msg_type, metadata_.messageType());

    EXPECT_TRUE(metadata_.hasSequenceId());
    EXPECT_EQ(seq_id, metadata_.sequenceId());

    EXPECT_FALSE(metadata_.hasFrameSize());
    EXPECT_FALSE(metadata_.hasProtocol());
    EXPECT_FALSE(metadata_.hasAppException());
    EXPECT_EQ(metadata_.headers().size(), 0);
  }

  void expectDefaultMetadata() { expectMetadata("-", MessageType::Oneway, 1); }

  MessageMetadata metadata_;
};

TEST_F(CompactProtocolTest, Name) {
  CompactProtocolImpl proto;
  EXPECT_EQ(proto.name(), "compact");
}

TEST_F(CompactProtocolTest, ReadMessageBegin) {
  CompactProtocolImpl proto;

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    addRepeated(buffer, 3, 'x');

    EXPECT_FALSE(proto.readMessageBegin(buffer, metadata_));
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 3);
  }

  // Wrong protocol version
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x0102);
    addRepeated(buffer, 2, 'x');

    EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, metadata_), EnvoyException,
                              "invalid compact protocol version 0x0102 != 0x8201");
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 4);
  }

  // Invalid message type
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    // Message type is encoded in the 3 highest order bits of the second byte.
    int8_t invalid_msg_type = static_cast<int8_t>(MessageType::LastMessageType) + 1;
    buffer.writeBEInt<int16_t>(static_cast<int16_t>(0x8201 | (invalid_msg_type << 5)));
    addRepeated(buffer, 2, 'x');

    EXPECT_THROW_WITH_MESSAGE(
        proto.readMessageBegin(buffer, metadata_), EnvoyException,
        fmt::format("invalid compact protocol message type {}", invalid_msg_type));
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 4);
  }

  // Insufficient data to read message id
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    addRepeated(buffer, 2, 0x81);

    EXPECT_FALSE(proto.readMessageBegin(buffer, metadata_));
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 4);
  }

  // Invalid sequence id encoding
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    addSeq(buffer, {0x81, 0x81, 0x81, 0x81, 0x81, 0}); // > 32 bit varint
    buffer.writeByte(0);

    EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, metadata_), EnvoyException,
                              "invalid compact protocol varint i32");
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 9);
  }

  // Insufficient data to read message name length
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    buffer.writeByte(32);
    buffer.writeByte(0x81); // unterminated varint

    EXPECT_FALSE(proto.readMessageBegin(buffer, metadata_));
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 4);
  }

  // Insufficient data to read message name
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    buffer.writeByte(32);
    buffer.writeByte(10);
    buffer.add("partial");

    EXPECT_FALSE(proto.readMessageBegin(buffer, metadata_));
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 11);
  }

  // Empty name
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    buffer.writeByte(32);
    buffer.writeByte(0);

    EXPECT_TRUE(proto.readMessageBegin(buffer, metadata_));
    expectMetadata("", MessageType::Call, 32);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Invalid name length encoding
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    buffer.writeByte(32);
    addSeq(buffer, {0x81, 0x81, 0x81, 0x81, 0x81, 0}); // > 32 bit varint

    EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, metadata_), EnvoyException,
                              "invalid compact protocol varint i32");
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 9);
  }

  // Invalid name length
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    buffer.writeByte(32);
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // -1

    EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, metadata_), EnvoyException,
                              "negative compact protocol message name length -1");
    expectDefaultMetadata();
    EXPECT_EQ(buffer.length(), 8);
  }

  // Named message
  {
    Buffer::OwnedImpl buffer;
    resetMetadata();

    buffer.writeBEInt<int16_t>(0x8221);
    buffer.writeBEInt<int16_t>(0x8202); // 0x0102
    buffer.writeByte(8);
    buffer.add("the_name");

    EXPECT_TRUE(proto.readMessageBegin(buffer, metadata_));
    expectMetadata("the_name", MessageType::Call, 0x102);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST_F(CompactProtocolTest, ReadMessageEnd) {
  Buffer::OwnedImpl buffer;
  CompactProtocolImpl proto;

  EXPECT_TRUE(proto.readMessageEnd(buffer));
}

TEST_F(CompactProtocolTest, ReadStruct) {
  Buffer::OwnedImpl buffer;
  CompactProtocolImpl proto;
  std::string name = "-";

  EXPECT_TRUE(proto.readStructBegin(buffer, name));
  EXPECT_EQ(name, "");

  EXPECT_TRUE(proto.readStructEnd(buffer));

  EXPECT_THROW_WITH_MESSAGE(proto.readStructEnd(buffer), EnvoyException,
                            "invalid check for compact protocol struct end")
}

TEST_F(CompactProtocolTest, ReadFieldBegin) {
  CompactProtocolImpl proto;

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    EXPECT_FALSE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
  }

  // Stop field
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0xF0);

    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(field_type, FieldType::Stop);
    EXPECT_EQ(field_id, 0);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Long-form field header, insufficient data
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0x05);

    EXPECT_FALSE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
    EXPECT_EQ(buffer.length(), 1);
  }

  // Long-form field header, insufficient data for field id (or invalid field id encoding)
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0x05);
    buffer.writeByte(0x81);

    EXPECT_FALSE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
    EXPECT_EQ(buffer.length(), 2);

    addRepeated(buffer, 4, 0x81);
    EXPECT_THROW_WITH_MESSAGE(proto.readFieldBegin(buffer, name, field_type, field_id),
                              EnvoyException, "invalid compact protocol zig-zag i32");
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
    EXPECT_EQ(buffer.length(), 6);
  }

  // Long-form field header, field id > 32767
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0x05);
    addSeq(buffer, {0x80, 0x80, 0x04}); // zigzag(0x10000) = 0x8000

    EXPECT_THROW_WITH_MESSAGE(proto.readFieldBegin(buffer, name, field_type, field_id),
                              EnvoyException, "invalid compact protocol field id 32768");
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
    EXPECT_EQ(buffer.length(), 4);
  }

  // Long-form field header, field id < 0
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0x05);
    addSeq(buffer, {0x01}); // zigzag(1) = -1

    EXPECT_THROW_WITH_MESSAGE(proto.readFieldBegin(buffer, name, field_type, field_id),
                              EnvoyException, "invalid compact protocol field id -1");
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
    EXPECT_EQ(buffer.length(), 2);
  }

  // Unknown compact protocol field type
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0x0D);
    buffer.writeByte(0x04);

    EXPECT_THROW_WITH_MESSAGE(proto.readFieldBegin(buffer, name, field_type, field_id),
                              EnvoyException, "unknown compact protocol field type 13");
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
    EXPECT_EQ(buffer.length(), 2);
  }

  // Valid long-form field-header
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0x05);
    buffer.writeByte(0x04);

    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(field_type, FieldType::I32);
    EXPECT_EQ(field_id, 2);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Valid short-form field header (must follow a valid long-form header)
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    buffer.writeByte(0xF5);

    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(field_type, FieldType::I32);
    EXPECT_EQ(field_id, 17);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST_F(CompactProtocolTest, ReadFieldEnd) {
  Buffer::OwnedImpl buffer;
  CompactProtocolImpl proto;
  EXPECT_TRUE(proto.readFieldEnd(buffer));
}

TEST_F(CompactProtocolTest, ReadMapBegin) {
  CompactProtocolImpl proto;

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0x81); // unterminated varint

    EXPECT_FALSE(proto.readMapBegin(buffer, key_type, value_type, size));
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 1);
  }

  // Invalid map size encoding
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    addSeq(buffer, {0x81, 0x81, 0x81, 0x81, 0x81, 0x00});

    EXPECT_THROW_WITH_MESSAGE(proto.readMapBegin(buffer, key_type, value_type, size),
                              EnvoyException, "invalid compact protocol varint i32");
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 6);
  }

  // Invalid map size
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // -1

    EXPECT_THROW_WITH_MESSAGE(proto.readMapBegin(buffer, key_type, value_type, size),
                              EnvoyException, "negative compact protocol map size -1");
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 5);
  }

  // Insufficient data after reading map size
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(2);

    EXPECT_FALSE(proto.readMapBegin(buffer, key_type, value_type, size));
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 1);
  }

  // Empty map
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0);

    EXPECT_TRUE(proto.readMapBegin(buffer, key_type, value_type, size));
    EXPECT_EQ(key_type, FieldType::Stop);
    EXPECT_EQ(value_type, FieldType::Stop);
    EXPECT_EQ(size, 0);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Non-empty map
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    addSeq(buffer, {0x80, 0x01}); // 0x80
    buffer.writeByte(0x57);

    EXPECT_TRUE(proto.readMapBegin(buffer, key_type, value_type, size));
    EXPECT_EQ(key_type, FieldType::I32);
    EXPECT_EQ(value_type, FieldType::Double);
    EXPECT_EQ(size, 128);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Unknown key type
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0x02);
    buffer.writeByte(0xD7);

    EXPECT_THROW_WITH_MESSAGE(proto.readMapBegin(buffer, key_type, value_type, size),
                              EnvoyException, "unknown compact protocol field type 13");
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 2);
  }

  // Unknown value type
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0x02);
    buffer.writeByte(0x5D);

    EXPECT_THROW_WITH_MESSAGE(proto.readMapBegin(buffer, key_type, value_type, size),
                              EnvoyException, "unknown compact protocol field type 13");
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 2);
  }
}

TEST_F(CompactProtocolTest, ReadMapEnd) {
  Buffer::OwnedImpl buffer;
  CompactProtocolImpl proto;
  EXPECT_TRUE(proto.readMapEnd(buffer));
}

TEST_F(CompactProtocolTest, ReadListBegin) {
  CompactProtocolImpl proto;

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    EXPECT_FALSE(proto.readListBegin(buffer, elem_type, size));
    EXPECT_EQ(elem_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Short-form list header
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0xE5);

    EXPECT_TRUE(proto.readListBegin(buffer, elem_type, size));
    EXPECT_EQ(elem_type, FieldType::I32);
    EXPECT_EQ(size, 14);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Long-form list header, insufficient data to read size
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0xF5);
    buffer.writeByte(0x81);

    EXPECT_FALSE(proto.readListBegin(buffer, elem_type, size));
    EXPECT_EQ(elem_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 2);
  }

  // Long-form list header, invalid size encoding
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0xF5);
    addSeq(buffer, {0x81, 0x81, 0x81, 0x81, 0x81, 0}); // > 32 bit varint

    EXPECT_THROW_WITH_MESSAGE(proto.readListBegin(buffer, elem_type, size), EnvoyException,
                              "invalid compact protocol varint i32");
    EXPECT_EQ(elem_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 7);
  }

  // Long-form list header, illegal size
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0xF5);
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // -1

    EXPECT_THROW_WITH_MESSAGE(proto.readListBegin(buffer, elem_type, size), EnvoyException,
                              "negative compact protocol list/set size -1");
    EXPECT_EQ(elem_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 6);
  }

  // Long-form list header
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0xF5);
    addSeq(buffer, {0x80, 0x01}); // 0x80

    EXPECT_TRUE(proto.readListBegin(buffer, elem_type, size));
    EXPECT_EQ(elem_type, FieldType::I32);
    EXPECT_EQ(size, 128);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Unknown list type
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    buffer.writeByte(0x1D);

    EXPECT_THROW_WITH_MESSAGE(proto.readListBegin(buffer, elem_type, size), EnvoyException,
                              "unknown compact protocol field type 13");
    EXPECT_EQ(elem_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 1);
  }
}

TEST_F(CompactProtocolTest, ReadListEnd) {
  Buffer::OwnedImpl buffer;
  CompactProtocolImpl proto;
  EXPECT_TRUE(proto.readListEnd(buffer));
}

TEST_F(CompactProtocolTest, ReadSetBegin) {
  CompactProtocolImpl proto;

  // Test only the happy path, since this method is just delegated to readListBegin()
  Buffer::OwnedImpl buffer;
  FieldType elem_type = FieldType::String;
  uint32_t size = 0;

  buffer.writeByte(0x15);

  EXPECT_TRUE(proto.readSetBegin(buffer, elem_type, size));
  EXPECT_EQ(elem_type, FieldType::I32);
  EXPECT_EQ(size, 1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST_F(CompactProtocolTest, ReadSetEnd) {
  Buffer::OwnedImpl buffer;
  CompactProtocolImpl proto;
  EXPECT_TRUE(proto.readSetEnd(buffer));
}

TEST_F(CompactProtocolTest, ReadBool) {
  CompactProtocolImpl proto;

  // Bool field values are encoded in the field type
  {
    Buffer::OwnedImpl buffer;
    std::string name;
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;
    bool value = false;

    buffer.writeByte(0x01);
    buffer.writeByte(0x04);

    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(field_type, FieldType::Bool);
    EXPECT_EQ(field_id, 2);
    EXPECT_EQ(buffer.length(), 0);

    EXPECT_TRUE(proto.readBool(buffer, value));
    EXPECT_TRUE(value);

    // readFieldEnd clears stored bool value
    EXPECT_TRUE(proto.readFieldEnd(buffer));
    EXPECT_FALSE(proto.readBool(buffer, value));

    buffer.writeByte(0x02);
    buffer.writeByte(0x06);

    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(field_type, FieldType::Bool);
    EXPECT_EQ(field_id, 3);
    EXPECT_EQ(buffer.length(), 0);

    EXPECT_TRUE(proto.readBool(buffer, value));
    EXPECT_FALSE(value);

    // readFieldEnd clears stored bool value
    EXPECT_TRUE(proto.readFieldEnd(buffer));
    EXPECT_FALSE(proto.readBool(buffer, value));
  }

  // Outside of the readFieldBegin/End pair (with boolean type), readBool expects a byte.
  {
    Buffer::OwnedImpl buffer;
    bool value = false;

    EXPECT_FALSE(proto.readBool(buffer, value));
    EXPECT_FALSE(value);

    buffer.writeByte(1);
    EXPECT_TRUE(proto.readBool(buffer, value));
    EXPECT_TRUE(value);
    EXPECT_EQ(buffer.length(), 0);

    buffer.writeByte(0);
    EXPECT_TRUE(proto.readBool(buffer, value));
    EXPECT_FALSE(value);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST_F(CompactProtocolTest, ReadIntegerTypes) {
  CompactProtocolImpl proto;

  // Byte
  {
    Buffer::OwnedImpl buffer;
    uint8_t value = 1;

    EXPECT_FALSE(proto.readByte(buffer, value));
    EXPECT_EQ(value, 1);

    buffer.writeByte(0);
    EXPECT_TRUE(proto.readByte(buffer, value));
    EXPECT_EQ(value, 0);
    EXPECT_EQ(buffer.length(), 0);

    buffer.writeByte(0xFF);
    EXPECT_TRUE(proto.readByte(buffer, value));
    EXPECT_EQ(value, 0xFF);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Int16
  {
    Buffer::OwnedImpl buffer;
    int16_t value = 1;

    // Insufficient data
    EXPECT_FALSE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, 1);

    // Still insufficient
    buffer.writeByte(0x80);
    EXPECT_FALSE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, 1);
    buffer.drain(1);

    addSeq(buffer, {0xFE, 0xFF, 0x03}); // zigzag(0xFFFE) = 0x7FFF
    EXPECT_TRUE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, 32767);
    EXPECT_EQ(buffer.length(), 0);

    addSeq(buffer, {0xFF, 0xFF, 0x03}); // zigzag(0xFFFF) = 0x8000
    EXPECT_TRUE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, -32768);
    EXPECT_EQ(buffer.length(), 0);

    // More than 32 bits
    value = 1;
    addSeq(buffer, {0x81, 0x81, 0x81, 0x81, 0x81, 0}); // > 32 bit varint
    EXPECT_THROW_WITH_MESSAGE(proto.readInt16(buffer, value), EnvoyException,
                              "invalid compact protocol zig-zag i32");
    EXPECT_EQ(value, 1);
    EXPECT_EQ(buffer.length(), 6);
    buffer.drain(6);

    // Within the encoding's range, but too large for i16
    value = 1;
    addSeq(buffer, {0xFE, 0xFF, 0x0F}); // zigzag(0x3FFFE) = 0x1FFFF
    EXPECT_THROW_WITH_MESSAGE(proto.readInt16(buffer, value), EnvoyException,
                              "compact protocol i16 exceeds allowable range 131071");
    EXPECT_EQ(buffer.length(), 3);
  }

  // Int32
  {
    Buffer::OwnedImpl buffer;
    int32_t value = 1;

    // Insufficient data
    EXPECT_FALSE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, 1);

    // Still insufficient
    buffer.writeByte(0x80);
    EXPECT_FALSE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, 1);
    buffer.drain(1);

    addSeq(buffer, {0xFE, 0xFF, 0xFF, 0xFF, 0x0F}); // zigzag(0xFFFFFFFE) = 0x7FFFFFFF
    EXPECT_TRUE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, std::numeric_limits<int32_t>::max());

    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x0F}); // zigzag(0xFFFFFFFF) = 0x80000000
    EXPECT_TRUE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, std::numeric_limits<int32_t>::min());

    // More than 32 bits
    value = 1;
    addSeq(buffer, {0x81, 0x81, 0x81, 0x81, 0x81, 0}); // > 32 bit varint
    EXPECT_THROW_WITH_MESSAGE(proto.readInt32(buffer, value), EnvoyException,
                              "invalid compact protocol zig-zag i32");
    EXPECT_EQ(value, 1);
    EXPECT_EQ(buffer.length(), 6);
  }

  // Int64
  {
    Buffer::OwnedImpl buffer;
    int64_t value = 1;

    // Insufficient data
    EXPECT_FALSE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, 1);

    // Still insufficient
    buffer.writeByte(0x80);
    EXPECT_FALSE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, 1);
    buffer.drain(1);

    // zigzag(0xFFFFFFFFFFFFFFFE) = 0x7FFFFFFFFFFFFFFF
    addSeq(buffer, {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01});
    EXPECT_TRUE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, std::numeric_limits<int64_t>::max());

    // zigzag(0xFFFFFFFFFFFFFFFF) = 0x8000000000000000
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01});
    EXPECT_TRUE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, std::numeric_limits<int64_t>::min());

    // More than 64 bits
    value = 1;
    addRepeated(buffer, 11, 0x81); // > 64 bit varint
    EXPECT_THROW_WITH_MESSAGE(proto.readInt64(buffer, value), EnvoyException,
                              "invalid compact protocol zig-zag i64");
    EXPECT_EQ(value, 1);
    EXPECT_EQ(buffer.length(), 11);
  }
}

TEST_F(CompactProtocolTest, ReadDouble) {
  CompactProtocolImpl proto;

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    double value = 1.0;
    addRepeated(buffer, 7, 0);
    EXPECT_FALSE(proto.readDouble(buffer, value));
    EXPECT_EQ(value, 1.0);
    EXPECT_EQ(buffer.length(), 7);
  }

  // double value
  {
    Buffer::OwnedImpl buffer;
    double value = 1.0;

    // 01000000 00001000 00000000 0000000 00000000 00000000 00000000 000000000 = 3
    // c.f. https://en.wikipedia.org/wiki/Double-precision_floating-point_format
    buffer.writeByte(0x40);
    buffer.writeByte(0x08);
    addRepeated(buffer, 6, 0);

    EXPECT_TRUE(proto.readDouble(buffer, value));
    EXPECT_EQ(value, 3.0);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST_F(CompactProtocolTest, ReadString) {
  CompactProtocolImpl proto;

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    EXPECT_FALSE(proto.readString(buffer, value));
    EXPECT_EQ(value, "-");
    EXPECT_EQ(buffer.length(), 0);
  }

  // Insufficient data to read length
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    buffer.writeByte(0x81);

    EXPECT_FALSE(proto.readString(buffer, value));
    EXPECT_EQ(value, "-");
    EXPECT_EQ(buffer.length(), 1);
  }

  // Insufficient data to read string
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    buffer.writeByte(0x4);

    EXPECT_FALSE(proto.readString(buffer, value));
    EXPECT_EQ(value, "-");
    EXPECT_EQ(buffer.length(), 1);
  }

  // Invalid length
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}); // -1

    EXPECT_THROW_WITH_MESSAGE(proto.readString(buffer, value), EnvoyException,
                              "negative compact protocol string/binary length -1");
    EXPECT_EQ(value, "-");
    EXPECT_EQ(buffer.length(), 5);
  }

  // empty string
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    buffer.writeByte(0);

    EXPECT_TRUE(proto.readString(buffer, value));
    EXPECT_EQ(value, "");
    EXPECT_EQ(buffer.length(), 0);
  }

  // non-empty string
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    buffer.writeByte(0x06);
    buffer.add("string");

    EXPECT_TRUE(proto.readString(buffer, value));
    EXPECT_EQ(value, "string");
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST_F(CompactProtocolTest, ReadBinary) {
  // Test only the happy path, since this method is just delegated to readString()
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  std::string value = "-";

  buffer.writeByte(0x06);
  buffer.add("string");

  EXPECT_TRUE(proto.readBinary(buffer, value));
  EXPECT_EQ(value, "string");
  EXPECT_EQ(buffer.length(), 0);
}

class CompactProtocolFieldTypeTest : public TestWithParam<uint8_t> {};

TEST_P(CompactProtocolFieldTypeTest, ConvertsToFieldType) {
  uint8_t compact_field_type = GetParam();

  CompactProtocolImpl proto;
  std::string name = "-";
  int8_t invalid_field_type = static_cast<int8_t>(FieldType::LastFieldType) + 1;
  FieldType field_type = static_cast<FieldType>(invalid_field_type);
  int16_t field_id = 0;

  {
    Buffer::OwnedImpl buffer;
    buffer.writeByte(compact_field_type);
    buffer.writeByte(0x02); // zigzag(2) = 1

    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_LE(field_type, FieldType::LastFieldType);
  }

  {
    // Long form field header
    Buffer::OwnedImpl buffer;
    proto.writeFieldBegin(buffer, "-", field_type, 100);
    if (field_type == FieldType::Bool) {
      proto.writeBool(buffer, compact_field_type == 1);
    }

    uint8_t* data = static_cast<uint8_t*>(buffer.linearize(1));
    EXPECT_NE(nullptr, data);
    EXPECT_EQ(compact_field_type, *data);
  }
}

INSTANTIATE_TEST_CASE_P(CompactFieldTypes, CompactProtocolFieldTypeTest,
                        Values(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));

TEST_F(CompactProtocolTest, WriteMessageBegin) {
  CompactProtocolImpl proto;

  // Named call
  {
    metadata_.setMethodName("message");
    metadata_.setMessageType(MessageType::Call);
    metadata_.setSequenceId(1);

    Buffer::OwnedImpl buffer;
    proto.writeMessageBegin(buffer, metadata_);
    EXPECT_EQ(std::string("\x82\x21\x1\x7message", 11), buffer.toString());
  }

  // Unnamed oneway
  {
    metadata_.setMethodName("");
    metadata_.setMessageType(MessageType::Oneway);
    metadata_.setSequenceId(2);

    Buffer::OwnedImpl buffer;
    proto.writeMessageBegin(buffer, metadata_);
    EXPECT_EQ(std::string("\x82\x81\x2\0", 4), buffer.toString());
  }
}

TEST_F(CompactProtocolTest, WriteMessageEnd) {
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeMessageEnd(buffer);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(CompactProtocolTest, WriteStruct) {
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;

  proto.writeStructBegin(buffer, "unused");
  proto.writeStructEnd(buffer);
  EXPECT_EQ(0, buffer.length());

  // struct begin/end always appear in nested pairs
  EXPECT_THROW_WITH_MESSAGE(proto.writeStructEnd(buffer), EnvoyException,
                            "invalid write of compact protocol struct end")
}

TEST_F(CompactProtocolTest, WriteFieldBegin) {
  // Stop field
  {
    CompactProtocolImpl proto;
    Buffer::OwnedImpl buffer;
    proto.writeFieldBegin(buffer, "unused", FieldType::Stop, 1);
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }

  {
    CompactProtocolImpl proto;

    // Short form
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::I32, 1);
      EXPECT_EQ("\x15", buffer.toString());
    }

    // Long form
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Struct, 17);
      EXPECT_EQ(std::string("\xC\x22", 2), buffer.toString());
    }

    // Short form
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Byte, 32);
      EXPECT_EQ("\xF3", buffer.toString());
    }

    // Short form
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::String, 33);
      EXPECT_EQ("\x18", buffer.toString());
    }
  }

  {
    CompactProtocolImpl proto;

    // Long form
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::I32, 16);
      EXPECT_EQ(std::string("\x5\x20", 2), buffer.toString());
    }

    // Short form
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Struct, 17);
      EXPECT_EQ("\x1C", buffer.toString());
    }

    // Long form
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Byte, 33);
      EXPECT_EQ(std::string("\x3\x42", 2), buffer.toString());
    }

    // Long form (3 bytes)
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::String, 64);
      EXPECT_EQ(std::string("\x8\x80\x1", 3), buffer.toString());
    }
  }

  // Unknown field type
  {
    CompactProtocolImpl proto;
    Buffer::OwnedImpl buffer;

    int8_t invalid_field_type = static_cast<int8_t>(FieldType::LastFieldType) + 1;
    FieldType field_type = static_cast<FieldType>(invalid_field_type);

    EXPECT_THROW_WITH_MESSAGE(proto.writeFieldBegin(buffer, "unused", field_type, 1),
                              EnvoyException,
                              fmt::format("unknown protocol field type {}", invalid_field_type));
  }
}

TEST_F(CompactProtocolTest, WriteFieldEnd) {
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeFieldEnd(buffer);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(CompactProtocolTest, WriteBoolField) {
  // Boolean struct fields are encoded with custom types to save a byte

  // Short form field
  {
    CompactProtocolImpl proto;
    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Bool, 8);
      EXPECT_EQ(0, buffer.length());
      proto.writeBool(buffer, true);
      EXPECT_EQ("\x81", buffer.toString());
    }

    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Bool, 12);
      EXPECT_EQ(0, buffer.length());
      proto.writeBool(buffer, false);
      EXPECT_EQ("\x42", buffer.toString());
    }
  }

  // Long form field
  {
    CompactProtocolImpl proto;

    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Bool, 16);
      EXPECT_EQ(0, buffer.length());
      proto.writeBool(buffer, true);
      EXPECT_EQ(std::string("\x1\x20", 2), buffer.toString());
    }

    {
      Buffer::OwnedImpl buffer;
      proto.writeFieldBegin(buffer, "unused", FieldType::Bool, 32);
      EXPECT_EQ(0, buffer.length());
      proto.writeBool(buffer, false);
      EXPECT_EQ(std::string("\x2\x40", 2), buffer.toString());
    }
  }
}

TEST_F(CompactProtocolTest, WriteMapBegin) {
  CompactProtocolImpl proto;

  // Empty map
  {
    Buffer::OwnedImpl buffer;
    proto.writeMapBegin(buffer, FieldType::I32, FieldType::Bool, 0);
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }

  // Non-empty map
  {
    Buffer::OwnedImpl buffer;
    proto.writeMapBegin(buffer, FieldType::I32, FieldType::Bool, 3);
    EXPECT_EQ("\3\x51", buffer.toString());
  }

  // Oversized map
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(
        proto.writeMapBegin(buffer, FieldType::I32, FieldType::Bool, 3000000000), EnvoyException,
        "illegal compact protocol map size 3000000000");
  }
}

TEST_F(CompactProtocolTest, WriteMapEnd) {
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeMapEnd(buffer);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(CompactProtocolTest, WriteListBegin) {
  CompactProtocolImpl proto;

  // Empty list
  {
    Buffer::OwnedImpl buffer;
    proto.writeListBegin(buffer, FieldType::I32, 0);
    EXPECT_EQ("\x5", buffer.toString());
  }

  // List (short form)
  {
    Buffer::OwnedImpl buffer;
    proto.writeListBegin(buffer, FieldType::I32, 14);
    EXPECT_EQ("\xE5", buffer.toString());
  }

  // List (long form)
  {
    Buffer::OwnedImpl buffer;
    proto.writeListBegin(buffer, FieldType::Bool, 15);
    EXPECT_EQ("\xF1\xF", buffer.toString());
  }

  // Oversized list
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(proto.writeListBegin(buffer, FieldType::I32, 3000000000),
                              EnvoyException, "illegal compact protocol list/set size 3000000000");
  }
}

TEST_F(CompactProtocolTest, WriteListEnd) {
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeListEnd(buffer);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(CompactProtocolTest, WriteSetBegin) {
  CompactProtocolImpl proto;

  // Empty set only, as writeSetBegin delegates to writeListBegin.
  Buffer::OwnedImpl buffer;
  proto.writeSetBegin(buffer, FieldType::I32, 0);
  EXPECT_EQ("\x5", buffer.toString());
}

TEST_F(CompactProtocolTest, WriteSetEnd) {
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeSetEnd(buffer);
  EXPECT_EQ(0, buffer.length());
}

TEST_F(CompactProtocolTest, WriteBool) {
  CompactProtocolImpl proto;

  // Non-field bools (see WriteBoolField test)
  {
    Buffer::OwnedImpl buffer;
    proto.writeBool(buffer, true);
    EXPECT_EQ("\x1", buffer.toString());
  }

  {
    Buffer::OwnedImpl buffer;
    proto.writeBool(buffer, false);
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }
}

TEST_F(CompactProtocolTest, WriteByte) {
  CompactProtocolImpl proto;

  {
    Buffer::OwnedImpl buffer;
    proto.writeByte(buffer, -1);
    EXPECT_EQ("\xFF", buffer.toString());
  }

  {
    Buffer::OwnedImpl buffer;
    proto.writeByte(buffer, 127);
    EXPECT_EQ("\x7F", buffer.toString());
  }
}

TEST_F(CompactProtocolTest, WriteInt16) {
  CompactProtocolImpl proto;

  // zigzag(1) = 2
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt16(buffer, 1);
    EXPECT_EQ("\x2", buffer.toString());
  }

  // zigzag(128) = 256 (0x200)
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt16(buffer, 128);
    EXPECT_EQ("\x80\x2", buffer.toString());
  }

  // zigzag(-1) = 1
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt16(buffer, -1);
    EXPECT_EQ("\x1", buffer.toString());
  }

  // zigzag(32767) = 65534 (0xFFFE)
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt16(buffer, std::numeric_limits<int16_t>::max());
    EXPECT_EQ("\xFE\xFF\x3", buffer.toString());
  }

  // zigzag(-32768) = 65535 (0xFFFF)
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt16(buffer, std::numeric_limits<int16_t>::min());
    EXPECT_EQ("\xFF\xFF\x3", buffer.toString());
  }
}

TEST_F(CompactProtocolTest, WriteInt32) {
  CompactProtocolImpl proto;

  // zigzag(1) = 2
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt32(buffer, 1);
    EXPECT_EQ("\x2", buffer.toString());
  }

  // zigzag(128) = 256 (0x200)
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt32(buffer, 128);
    EXPECT_EQ("\x80\x2", buffer.toString());
  }

  // zigzag(-1) = 1
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt32(buffer, -1);
    EXPECT_EQ("\x1", buffer.toString());
  }

  // zigzag(0x7FFFFFFF) = 0xFFFFFFFE
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt32(buffer, std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\xFE\xFF\xFF\xFF\xF", buffer.toString());
  }

  // zigzag(0x80000000) = 0xFFFFFFFF
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt32(buffer, std::numeric_limits<int32_t>::min());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xF", buffer.toString());
  }
}

TEST_F(CompactProtocolTest, WriteInt64) {
  CompactProtocolImpl proto;

  // zigzag(1) = 2
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt64(buffer, 1);
    EXPECT_EQ("\x2", buffer.toString());
  }

  // zigzag(128) = 256 (0x200)
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt64(buffer, 128);
    EXPECT_EQ("\x80\x2", buffer.toString());
  }

  // zigzag(-1) = 1
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt64(buffer, -1);
    EXPECT_EQ("\x1", buffer.toString());
  }

  // zigzag(0x7FFFFFFF FFFFFFFF) = 0xFFFFFFFF FFFFFFFE
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt64(buffer, std::numeric_limits<int64_t>::max());
    EXPECT_EQ("\xFE\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1", buffer.toString());
  }

  // zigzag(0x80000000 00000000) = 0xFFFFFFFF FFFFFFFF
  {
    Buffer::OwnedImpl buffer;
    proto.writeInt64(buffer, std::numeric_limits<int64_t>::min());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1", buffer.toString());
  }
}

TEST_F(CompactProtocolTest, WriteDouble) {
  CompactProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeDouble(buffer, 3.0);
  EXPECT_EQ(std::string("\x40\x8\0\0\0\0\0\0", 8), buffer.toString());
}

TEST_F(CompactProtocolTest, WriteString) {
  CompactProtocolImpl proto;

  {
    Buffer::OwnedImpl buffer;
    proto.writeString(buffer, "abc");
    EXPECT_EQ(std::string("\x3"
                          "abc",
                          4),
              buffer.toString());
  }

  {
    Buffer::OwnedImpl buffer;
    std::string data(192, 'a');
    proto.writeString(buffer, data);
    EXPECT_EQ(std::string("\xC0\x1") + data, buffer.toString());
  }

  {
    Buffer::OwnedImpl buffer;
    proto.writeString(buffer, "");
    EXPECT_EQ(std::string("\0", 1), buffer.toString());
  }
}

TEST_F(CompactProtocolTest, WriteBinary) {
  CompactProtocolImpl proto;

  // writeBinary is an alias for writeString
  Buffer::OwnedImpl buffer;
  proto.writeBinary(buffer, "abc");
  EXPECT_EQ(std::string("\x3"
                        "abc",
                        4),
            buffer.toString());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
