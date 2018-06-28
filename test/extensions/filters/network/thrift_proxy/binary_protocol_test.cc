#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/binary_protocol.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(BinaryProtocolTest, Name) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  EXPECT_EQ(proto.name(), "binary");
}

TEST(BinaryProtocolTest, ReadMessageBegin) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addRepeated(buffer, 11, 'x');

    EXPECT_FALSE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(msg_type, MessageType::Oneway);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 11);
  }

  // Wrong protocol version
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt16(buffer, 0x0102);
    addRepeated(buffer, 10, 'x');

    EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, name, msg_type, seq_id),
                              EnvoyException, "invalid binary protocol version 0x0102 != 0x8001");
    EXPECT_EQ(name, "-");
    EXPECT_EQ(msg_type, MessageType::Oneway);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 12);
  }

  // Invalid message type
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt16(buffer, 0x8001);
    addInt8(buffer, 'x');
    addInt8(buffer, static_cast<int8_t>(MessageType::LastMessageType) + 1);
    addRepeated(buffer, 8, 'x');

    EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, name, msg_type, seq_id),
                              EnvoyException,
                              fmt::format("invalid binary protocol message type {}",
                                          static_cast<int8_t>(MessageType::LastMessageType) + 1));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(msg_type, MessageType::Oneway);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 12);
  }

  // Empty name
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt16(buffer, 0x8001);
    addInt8(buffer, 'x');
    addInt8(buffer, MessageType::Call);
    addInt32(buffer, 0);
    addInt32(buffer, 1234);

    EXPECT_CALL(cb, messageStart(absl::string_view(""), MessageType::Call, 1234));
    EXPECT_TRUE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(msg_type, MessageType::Call);
    EXPECT_EQ(seq_id, 1234);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Insufficient data after checking name length
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt16(buffer, 0x8001);
    addInt8(buffer, 'x');
    addInt8(buffer, MessageType::Call);
    addInt32(buffer, 4); // name length
    addString(buffer, "abcd");

    EXPECT_FALSE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(msg_type, MessageType::Oneway);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 12);
  }

  // Named message
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt16(buffer, 0x8001);
    addInt8(buffer, 0);
    addInt8(buffer, MessageType::Call);
    addInt32(buffer, 8);
    addString(buffer, "the_name");
    addInt32(buffer, 5678);

    EXPECT_CALL(cb, messageStart(absl::string_view("the_name"), MessageType::Call, 5678));
    EXPECT_TRUE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "the_name");
    EXPECT_EQ(msg_type, MessageType::Call);
    EXPECT_EQ(seq_id, 5678);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST(BinaryProtocolTest, ReadMessageEnd) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

  EXPECT_CALL(cb, messageComplete());
  EXPECT_TRUE(proto.readMessageEnd(buffer));
}

TEST(BinaryProtocolTest, ReadStructBegin) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  std::string name = "-";
  EXPECT_CALL(cb, structBegin(absl::string_view("")));
  EXPECT_TRUE(proto.readStructBegin(buffer, name));
  EXPECT_EQ(name, "");
}

TEST(BinaryProtocolTest, ReadStructEnd) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  EXPECT_CALL(cb, structEnd());
  EXPECT_TRUE(proto.readStructEnd(buffer));
}

TEST(BinaryProtocolTest, ReadFieldBegin) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

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

    addInt8(buffer, FieldType::Stop);

    EXPECT_CALL(cb, structField(absl::string_view(""), FieldType::Stop, 0));
    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(field_type, FieldType::Stop);
    EXPECT_EQ(field_id, 0);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Insufficient data for non-stop field
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    addInt8(buffer, FieldType::I32);

    EXPECT_FALSE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
  }

  // Non-stop field
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    addInt8(buffer, FieldType::I32);
    addInt16(buffer, 99);

    EXPECT_CALL(cb, structField(absl::string_view(""), FieldType::I32, 99));
    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(field_type, FieldType::I32);
    EXPECT_EQ(field_id, 99);
    EXPECT_EQ(buffer.length(), 0);
  }

  // field id < 0
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    FieldType field_type = FieldType::String;
    int16_t field_id = 1;

    addInt8(buffer, FieldType::I32);
    addInt16(buffer, -1);

    EXPECT_THROW_WITH_MESSAGE(proto.readFieldBegin(buffer, name, field_type, field_id),
                              EnvoyException, "invalid binary protocol field id -1");
    EXPECT_EQ(name, "-");
    EXPECT_EQ(field_type, FieldType::String);
    EXPECT_EQ(field_id, 1);
    EXPECT_EQ(buffer.length(), 3);
  }
}

TEST(BinaryProtocolTest, ReadFieldEnd) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  EXPECT_TRUE(proto.readFieldEnd(buffer));
}

TEST(BinaryProtocolTest, ReadMapBegin) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    addRepeated(buffer, 5, 0);

    EXPECT_FALSE(proto.readMapBegin(buffer, key_type, value_type, size));
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 5);
  }

  // Invalid map size
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    addInt8(buffer, FieldType::I32);
    addInt8(buffer, FieldType::I32);
    addInt32(buffer, -1);

    EXPECT_THROW_WITH_MESSAGE(proto.readMapBegin(buffer, key_type, value_type, size),
                              EnvoyException, "negative binary protocol map size -1");
    EXPECT_EQ(key_type, FieldType::String);
    EXPECT_EQ(value_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 6);
  }

  // Valid map start
  {
    Buffer::OwnedImpl buffer;
    FieldType key_type = FieldType::String;
    FieldType value_type = FieldType::String;
    uint32_t size = 1;

    addInt8(buffer, FieldType::I32);
    addInt8(buffer, FieldType::Double);
    addInt32(buffer, 10);

    EXPECT_TRUE(proto.readMapBegin(buffer, key_type, value_type, size));
    EXPECT_EQ(key_type, FieldType::I32);
    EXPECT_EQ(value_type, FieldType::Double);
    EXPECT_EQ(size, 10);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST(BinaryProtocolTest, ReadMapEnd) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  EXPECT_TRUE(proto.readMapEnd(buffer));
}

TEST(BinaryProtocolTest, ReadListBegin) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    addRepeated(buffer, 4, 0);

    EXPECT_FALSE(proto.readListBegin(buffer, elem_type, size));
    EXPECT_EQ(elem_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 4);
  }

  // Invalid list size
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    addInt8(buffer, FieldType::I32);
    addInt32(buffer, -1);

    EXPECT_THROW_WITH_MESSAGE(proto.readListBegin(buffer, elem_type, size), EnvoyException,
                              "negative binary protocol list/set size -1");
    EXPECT_EQ(elem_type, FieldType::String);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer.length(), 5);
  }

  // Valid list start
  {
    Buffer::OwnedImpl buffer;
    FieldType elem_type = FieldType::String;
    uint32_t size = 1;

    addInt8(buffer, FieldType::I32);
    addInt32(buffer, 10);

    EXPECT_TRUE(proto.readListBegin(buffer, elem_type, size));
    EXPECT_EQ(elem_type, FieldType::I32);
    EXPECT_EQ(size, 10);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST(BinaryProtocolTest, ReadListEnd) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  EXPECT_TRUE(proto.readListEnd(buffer));
}

TEST(BinaryProtocolTest, ReadSetBegin) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

  // Test only the happy path, since this method is just delegated to readListBegin()
  Buffer::OwnedImpl buffer;
  FieldType elem_type = FieldType::String;
  uint32_t size = 1;

  addInt8(buffer, FieldType::I32);
  addInt32(buffer, 10);

  EXPECT_TRUE(proto.readSetBegin(buffer, elem_type, size));
  EXPECT_EQ(elem_type, FieldType::I32);
  EXPECT_EQ(size, 10);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BinaryProtocolTest, ReadSetEnd) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  EXPECT_TRUE(proto.readSetEnd(buffer));
}

TEST(BinaryProtocolTest, ReadIntegerTypes) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

  // Bool
  {
    Buffer::OwnedImpl buffer;
    bool value = false;

    EXPECT_FALSE(proto.readBool(buffer, value));
    EXPECT_FALSE(value);

    addInt8(buffer, 1);
    EXPECT_TRUE(proto.readBool(buffer, value));
    EXPECT_TRUE(value);
    EXPECT_EQ(buffer.length(), 0);

    addInt8(buffer, 0);
    EXPECT_TRUE(proto.readBool(buffer, value));
    EXPECT_FALSE(value);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Byte
  {
    Buffer::OwnedImpl buffer;
    uint8_t value = 1;

    EXPECT_FALSE(proto.readByte(buffer, value));
    EXPECT_EQ(value, 1);

    addInt8(buffer, 0);
    EXPECT_TRUE(proto.readByte(buffer, value));
    EXPECT_EQ(value, 0);
    EXPECT_EQ(buffer.length(), 0);

    addInt8(buffer, 0xFF);
    EXPECT_TRUE(proto.readByte(buffer, value));
    EXPECT_EQ(value, 0xFF);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Int16
  {
    Buffer::OwnedImpl buffer;
    int16_t value = 1;

    addInt8(buffer, 0);
    EXPECT_FALSE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, 1);

    addInt8(buffer, 0);
    EXPECT_TRUE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, 0);
    EXPECT_EQ(buffer.length(), 0);

    addInt8(buffer, 0x01);
    addInt8(buffer, 0x02);
    EXPECT_TRUE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, 0x0102);
    EXPECT_EQ(buffer.length(), 0);

    addRepeated(buffer, 2, 0xFF);
    EXPECT_TRUE(proto.readInt16(buffer, value));
    EXPECT_EQ(value, -1);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Int32
  {
    Buffer::OwnedImpl buffer;
    int32_t value = 1;

    addRepeated(buffer, 3, 0);
    EXPECT_FALSE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, 1);

    addInt8(buffer, 0);
    EXPECT_TRUE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, 0);
    EXPECT_EQ(buffer.length(), 0);

    addSeq(buffer, {0x01, 0x02, 0x03, 0x04});
    EXPECT_TRUE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, 0x01020304);
    EXPECT_EQ(buffer.length(), 0);

    addRepeated(buffer, 4, 0xFF);
    EXPECT_TRUE(proto.readInt32(buffer, value));
    EXPECT_EQ(value, -1);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Int64
  {
    Buffer::OwnedImpl buffer;
    int64_t value = 1;

    addRepeated(buffer, 7, 0);
    EXPECT_FALSE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, 1);

    addInt8(buffer, 0);
    EXPECT_TRUE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, 0);
    EXPECT_EQ(buffer.length(), 0);

    addSeq(buffer, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    EXPECT_TRUE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, 0x0102030405060708);
    EXPECT_EQ(buffer.length(), 0);

    addRepeated(buffer, 8, 0xFF);
    EXPECT_TRUE(proto.readInt64(buffer, value));
    EXPECT_EQ(value, -1);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST(BinaryProtocolTest, ReadDouble) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

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
    addSeq(buffer, {0x40, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    EXPECT_TRUE(proto.readDouble(buffer, value));
    EXPECT_EQ(value, 3.0);
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST(BinaryProtocolTest, ReadString) {
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);

  // Insufficient data to read length
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    addRepeated(buffer, 3, 0);

    EXPECT_FALSE(proto.readString(buffer, value));
    EXPECT_EQ(value, "-");
    EXPECT_EQ(buffer.length(), 3);
  }

  // Insufficient data to read string
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    addInt32(buffer, 1);

    EXPECT_FALSE(proto.readString(buffer, value));
    EXPECT_EQ(value, "-");
    EXPECT_EQ(buffer.length(), 4);
  }

  // Invalid length
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    addInt32(buffer, -1);

    EXPECT_THROW_WITH_MESSAGE(proto.readString(buffer, value), EnvoyException,
                              "negative binary protocol string/binary length -1");
    EXPECT_EQ(value, "-");
    EXPECT_EQ(buffer.length(), 4);
  }

  // empty string
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    addInt32(buffer, 0);

    EXPECT_TRUE(proto.readString(buffer, value));
    EXPECT_EQ(value, "");
    EXPECT_EQ(buffer.length(), 0);
  }

  // non-empty string
  {
    Buffer::OwnedImpl buffer;
    std::string value = "-";

    addInt32(buffer, 6);
    addString(buffer, "string");

    EXPECT_TRUE(proto.readString(buffer, value));
    EXPECT_EQ(value, "string");
    EXPECT_EQ(buffer.length(), 0);
  }
}

TEST(BinaryProtocolTest, ReadBinary) {
  // Test only the happy path, since this method is just delegated to readString()
  StrictMock<MockProtocolCallbacks> cb;
  BinaryProtocolImpl proto(cb);
  Buffer::OwnedImpl buffer;
  std::string value = "-";

  addInt32(buffer, 6);
  addString(buffer, "binary");

  EXPECT_TRUE(proto.readBinary(buffer, value));
  EXPECT_EQ(value, "binary");
  EXPECT_EQ(buffer.length(), 0);
}

TEST(LaxBinaryProtocolTest, Name) {
  StrictMock<MockProtocolCallbacks> cb;
  LaxBinaryProtocolImpl proto(cb);
  EXPECT_EQ(proto.name(), "binary/non-strict");
}

TEST(LaxBinaryProtocolTest, ReadMessageBegin) {
  StrictMock<MockProtocolCallbacks> cb;
  LaxBinaryProtocolImpl proto(cb);

  // Insufficient data
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addRepeated(buffer, 8, 'x');

    EXPECT_FALSE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(msg_type, MessageType::Oneway);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 8);
  }

  // Invalid message type
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt32(buffer, 0);
    addInt8(buffer, static_cast<int8_t>(MessageType::LastMessageType) + 1);
    addRepeated(buffer, 4, 'x');

    EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, name, msg_type, seq_id),
                              EnvoyException,
                              fmt::format("invalid (lax) binary protocol message type {}",
                                          static_cast<int8_t>(MessageType::LastMessageType) + 1));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(msg_type, MessageType::Oneway);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 9);
  }

  // Empty name
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt32(buffer, 0);
    addInt8(buffer, MessageType::Call);
    addInt32(buffer, 1234);

    EXPECT_CALL(cb, messageStart(absl::string_view(""), MessageType::Call, 1234));
    EXPECT_TRUE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "");
    EXPECT_EQ(msg_type, MessageType::Call);
    EXPECT_EQ(seq_id, 1234);
    EXPECT_EQ(buffer.length(), 0);
  }

  // Insufficient data after checking name length
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt32(buffer, 1); // name length
    addInt8(buffer, MessageType::Call);
    addInt32(buffer, 1234);

    EXPECT_FALSE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "-");
    EXPECT_EQ(msg_type, MessageType::Oneway);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 9);
  }

  // Named message
  {
    Buffer::OwnedImpl buffer;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    addInt32(buffer, 8);
    addString(buffer, "the_name");
    addInt8(buffer, MessageType::Call);
    addInt32(buffer, 5678);

    EXPECT_CALL(cb, messageStart(absl::string_view("the_name"), MessageType::Call, 5678));
    EXPECT_TRUE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "the_name");
    EXPECT_EQ(msg_type, MessageType::Call);
    EXPECT_EQ(seq_id, 5678);
    EXPECT_EQ(buffer.length(), 0);
  }
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
