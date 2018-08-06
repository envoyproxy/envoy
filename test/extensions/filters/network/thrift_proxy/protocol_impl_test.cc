#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/protocol_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(ProtocolNames, FromType) {
  for (int i = 0; i <= static_cast<int>(ProtocolType::LastProtocolType); i++) {
    ProtocolType type = static_cast<ProtocolType>(i);
    EXPECT_NE("", ProtocolNames::get().fromType(type));
  }
}

TEST(AutoProtocolTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  AutoProtocolImpl proto;
  std::string name = "-";
  MessageType msg_type = MessageType::Oneway;
  int32_t seq_id = -1;

  buffer.writeByte(0);
  EXPECT_FALSE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
  EXPECT_EQ(name, "-");
  EXPECT_EQ(msg_type, MessageType::Oneway);
  EXPECT_EQ(seq_id, -1);
}

TEST(AutoProtocolTest, UnknownProtocol) {
  Buffer::OwnedImpl buffer;
  AutoProtocolImpl proto;
  std::string name = "-";
  MessageType msg_type = MessageType::Oneway;
  int32_t seq_id = -1;

  buffer.writeBEInt<int16_t>(0x0102);

  EXPECT_THROW_WITH_MESSAGE(proto.readMessageBegin(buffer, name, msg_type, seq_id), EnvoyException,
                            "unknown thrift auto protocol message start 0102");
  EXPECT_EQ(name, "-");
  EXPECT_EQ(msg_type, MessageType::Oneway);
  EXPECT_EQ(seq_id, -1);
}

TEST(AutoProtocolTest, ReadMessageBegin) {
  // Binary Protocol
  {
    AutoProtocolImpl proto;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = -1;

    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(0x8001);
    buffer.writeByte(0);
    buffer.writeByte(MessageType::Call);
    buffer.writeBEInt<int32_t>(8);
    addString(buffer, "the_name");
    buffer.writeBEInt<int32_t>(1);

    EXPECT_TRUE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "the_name");
    EXPECT_EQ(msg_type, MessageType::Call);
    EXPECT_EQ(seq_id, 1);
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(proto.name(), "binary(auto)");
    EXPECT_EQ(proto.type(), ProtocolType::Binary);
  }

  // Compact protocol
  {
    AutoProtocolImpl proto;
    std::string name = "-";
    MessageType msg_type = MessageType::Oneway;
    int32_t seq_id = 1;

    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(0x8221);
    buffer.writeBEInt<int16_t>(0x8202); // 0x0102
    buffer.writeByte(8);
    addString(buffer, "the_name");

    EXPECT_TRUE(proto.readMessageBegin(buffer, name, msg_type, seq_id));
    EXPECT_EQ(name, "the_name");
    EXPECT_EQ(msg_type, MessageType::Call);
    EXPECT_EQ(seq_id, 0x0102);
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(proto.name(), "compact(auto)");
    EXPECT_EQ(proto.type(), ProtocolType::Compact);
  }
}

TEST(AutoProtocolTest, ReadDelegation) {
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  AutoProtocolImpl auto_proto;
  auto_proto.setProtocol(ProtocolPtr{proto});

  // readMessageBegin
  Buffer::OwnedImpl buffer;
  std::string name = "x";
  MessageType msg_type = MessageType::Call;
  int32_t seq_id = 1;

  EXPECT_CALL(*proto, readMessageBegin(Ref(buffer), Ref(name), Ref(msg_type), Ref(seq_id)))
      .WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readMessageBegin(buffer, name, msg_type, seq_id));

  // readMessageEnd
  EXPECT_CALL(*proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readMessageEnd(buffer));

  // readStructBegin
  EXPECT_CALL(*proto, readStructBegin(Ref(buffer), Ref(name))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readStructBegin(buffer, name));

  // readStructEnd
  EXPECT_CALL(*proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readStructEnd(buffer));

  // readFieldBegin
  FieldType field_type = FieldType::Stop;
  int16_t field_id = 1;

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), Ref(name), Ref(field_type), Ref(field_id)))
      .WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readFieldBegin(buffer, name, field_type, field_id));

  // readFieldEnd
  EXPECT_CALL(*proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readFieldEnd(buffer));

  // readMapBegin
  FieldType value_type = FieldType::Stop;
  uint32_t size = 1;

  EXPECT_CALL(*proto, readMapBegin(Ref(buffer), Ref(field_type), Ref(value_type), Ref(size)))
      .WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readMapBegin(buffer, field_type, value_type, size));

  // readMapEnd
  EXPECT_CALL(*proto, readMapEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readMapEnd(buffer));

  // readListBegin
  EXPECT_CALL(*proto, readListBegin(Ref(buffer), Ref(field_type), Ref(size)))
      .WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readListBegin(buffer, field_type, size));

  // readListEnd
  EXPECT_CALL(*proto, readListEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readListEnd(buffer));

  // readSetBegin
  EXPECT_CALL(*proto, readSetBegin(Ref(buffer), Ref(field_type), Ref(size))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readSetBegin(buffer, field_type, size));

  // readSetEnd
  EXPECT_CALL(*proto, readSetEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_TRUE(auto_proto.readSetEnd(buffer));

  // readBool
  {
    bool value;
    EXPECT_CALL(*proto, readBool(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readBool(buffer, value));
  }

  // readByte
  {
    uint8_t value;
    EXPECT_CALL(*proto, readByte(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readByte(buffer, value));
  }

  // readInt16
  {
    int16_t value;
    EXPECT_CALL(*proto, readInt16(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readInt16(buffer, value));
  }

  // readInt32
  {
    int32_t value;
    EXPECT_CALL(*proto, readInt32(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readInt32(buffer, value));
  }

  // readInt64
  {
    int64_t value;
    EXPECT_CALL(*proto, readInt64(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readInt64(buffer, value));
  }

  // readDouble
  {
    double value;
    EXPECT_CALL(*proto, readDouble(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readDouble(buffer, value));
  }

  // readString
  {
    std::string value = "x";
    EXPECT_CALL(*proto, readString(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readString(buffer, value));
  }

  // readBinary
  {
    std::string value = "x";
    EXPECT_CALL(*proto, readBinary(Ref(buffer), Ref(value))).WillOnce(Return(true));
    EXPECT_TRUE(auto_proto.readBinary(buffer, value));
  }
}

TEST(AutoProtocolTest, WriteDelegation) {
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  AutoProtocolImpl auto_proto;
  auto_proto.setProtocol(ProtocolPtr{proto});

  // writeMessageBegin
  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*proto, writeMessageBegin(Ref(buffer), "name", MessageType::Call, 100));
  auto_proto.writeMessageBegin(buffer, "name", MessageType::Call, 100);

  // writeMessageEnd
  EXPECT_CALL(*proto, writeMessageEnd(Ref(buffer)));
  auto_proto.writeMessageEnd(buffer);

  // writeStructBegin
  EXPECT_CALL(*proto, writeStructBegin(Ref(buffer), "name"));
  auto_proto.writeStructBegin(buffer, "name");

  // writeStructEnd
  EXPECT_CALL(*proto, writeStructEnd(Ref(buffer)));
  auto_proto.writeStructEnd(buffer);

  // writeFieldBegin
  EXPECT_CALL(*proto, writeFieldBegin(Ref(buffer), "name", FieldType::Stop, 100));
  auto_proto.writeFieldBegin(buffer, "name", FieldType::Stop, 100);

  // writeFieldEnd
  EXPECT_CALL(*proto, writeFieldEnd(Ref(buffer)));
  auto_proto.writeFieldEnd(buffer);

  // writeMapBegin
  EXPECT_CALL(*proto, writeMapBegin(Ref(buffer), FieldType::I32, FieldType::String, 100));
  auto_proto.writeMapBegin(buffer, FieldType::I32, FieldType::String, 100);

  // writeMapEnd
  EXPECT_CALL(*proto, writeMapEnd(Ref(buffer)));
  auto_proto.writeMapEnd(buffer);

  // writeListBegin
  EXPECT_CALL(*proto, writeListBegin(Ref(buffer), FieldType::String, 100));
  auto_proto.writeListBegin(buffer, FieldType::String, 100);

  // writeListEnd
  EXPECT_CALL(*proto, writeListEnd(Ref(buffer)));
  auto_proto.writeListEnd(buffer);

  // writeSetBegin
  EXPECT_CALL(*proto, writeSetBegin(Ref(buffer), FieldType::String, 100));
  auto_proto.writeSetBegin(buffer, FieldType::String, 100);

  // writeSetEnd
  EXPECT_CALL(*proto, writeSetEnd(Ref(buffer)));
  auto_proto.writeSetEnd(buffer);

  // writeBool
  EXPECT_CALL(*proto, writeBool(Ref(buffer), true));
  auto_proto.writeBool(buffer, true);

  // writeByte
  EXPECT_CALL(*proto, writeByte(Ref(buffer), 100));
  auto_proto.writeByte(buffer, 100);

  // writeInt16
  EXPECT_CALL(*proto, writeInt16(Ref(buffer), 100));
  auto_proto.writeInt16(buffer, 100);

  // writeInt32
  EXPECT_CALL(*proto, writeInt32(Ref(buffer), 100));
  auto_proto.writeInt32(buffer, 100);

  // writeInt64
  EXPECT_CALL(*proto, writeInt64(Ref(buffer), 100));
  auto_proto.writeInt64(buffer, 100);

  // writeDouble
  EXPECT_CALL(*proto, writeDouble(Ref(buffer), 10.0));
  auto_proto.writeDouble(buffer, 10.0);

  // writeString
  EXPECT_CALL(*proto, writeString(Ref(buffer), "string"));
  auto_proto.writeString(buffer, "string");

  // writeBinary
  EXPECT_CALL(*proto, writeBinary(Ref(buffer), "binary"));
  auto_proto.writeBinary(buffer, "binary");
}

TEST(AutoProtocolTest, Name) {
  AutoProtocolImpl proto;
  EXPECT_EQ(proto.name(), "auto");
}

TEST(AutoProtocolTest, Type) {
  AutoProtocolImpl proto;
  EXPECT_EQ(proto.type(), ProtocolType::Auto);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
