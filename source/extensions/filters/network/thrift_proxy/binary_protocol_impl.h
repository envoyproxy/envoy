#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "source/extensions/filters/network/thrift_proxy/protocol.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * BinaryProtocolImpl implements the Thrift Binary protocol with strict message encoding.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-binary-protocol.md
 */
class BinaryProtocolImpl : public Protocol {
public:
  BinaryProtocolImpl() = default;

  // Protocol
  const std::string& name() const override { return ProtocolNames::get().BINARY; }
  ProtocolType type() const override { return ProtocolType::Binary; }
  bool readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) override;
  bool readMessageEnd(Buffer::Instance& buffer) override;
  bool peekReplyPayload(Buffer::Instance& buffer, ReplyType& reply_type) override;
  bool readStructBegin(Buffer::Instance& buffer, std::string& name) override;
  bool readStructEnd(Buffer::Instance& buffer) override;
  bool readFieldBegin(Buffer::Instance& buffer, std::string& name, FieldType& field_type,
                      int16_t& field_id) override;
  bool readFieldEnd(Buffer::Instance& buffer) override;
  bool readMapBegin(Buffer::Instance& buffer, FieldType& key_type, FieldType& value_type,
                    uint32_t& size) override;
  bool readMapEnd(Buffer::Instance& buffer) override;
  bool readListBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) override;
  bool readListEnd(Buffer::Instance& buffer) override;
  bool readSetBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) override;
  bool readSetEnd(Buffer::Instance& buffer) override;
  bool readBool(Buffer::Instance& buffer, bool& value) override;
  bool readByte(Buffer::Instance& buffer, uint8_t& value) override;
  bool readInt16(Buffer::Instance& buffer, int16_t& value) override;
  bool readInt32(Buffer::Instance& buffer, int32_t& value) override;
  bool readInt64(Buffer::Instance& buffer, int64_t& value) override;
  bool readDouble(Buffer::Instance& buffer, double& value) override;
  bool readString(Buffer::Instance& buffer, std::string& value) override;
  bool readBinary(Buffer::Instance& buffer, std::string& value) override;
  void writeMessageBegin(Buffer::Instance& buffer, const MessageMetadata& metadata) override;
  void writeMessageEnd(Buffer::Instance& buffer) override;
  void writeStructBegin(Buffer::Instance& buffer, const std::string& name) override;
  void writeStructEnd(Buffer::Instance& buffer) override;
  void writeFieldBegin(Buffer::Instance& buffer, const std::string& name, FieldType field_type,
                       int16_t field_id) override;
  void writeFieldEnd(Buffer::Instance& buffer) override;
  void writeMapBegin(Buffer::Instance& buffer, FieldType key_type, FieldType value_type,
                     uint32_t size) override;
  void writeMapEnd(Buffer::Instance& buffer) override;
  void writeListBegin(Buffer::Instance& buffer, FieldType elem_type, uint32_t size) override;
  void writeListEnd(Buffer::Instance& buffer) override;
  void writeSetBegin(Buffer::Instance& buffer, FieldType elem_type, uint32_t size) override;
  void writeSetEnd(Buffer::Instance& buffer) override;
  void writeBool(Buffer::Instance& buffer, bool value) override;
  void writeByte(Buffer::Instance& buffer, uint8_t value) override;
  void writeInt16(Buffer::Instance& buffer, int16_t value) override;
  void writeInt32(Buffer::Instance& buffer, int32_t value) override;
  void writeInt64(Buffer::Instance& buffer, int64_t value) override;
  void writeDouble(Buffer::Instance& buffer, double value) override;
  void writeString(Buffer::Instance& buffer, const std::string& value) override;
  void writeBinary(Buffer::Instance& buffer, const std::string& value) override;

  static bool isMagic(uint16_t word) { return word == Magic; }

  // Minimum message length:
  //   version: 2 bytes +
  //   unused: 1 byte +
  //   msg type: 1 byte +
  //   name len: 4 bytes +
  //   name: 0 bytes +
  //   seq id: 4 bytes
  static constexpr uint64_t MinMessageBeginLength = 12;

private:
  static void validateFieldId(int16_t id);

  const static uint16_t Magic;
};

/**
 * LaxBinaryProtocolImpl implements the Thrift Binary protocol with non-strict (e.g. lax) message
 * encoding. See https://github.com/apache/thrift/blob/master/doc/specs/thrift-binary-protocol.md
 */
class LaxBinaryProtocolImpl : public BinaryProtocolImpl {
public:
  LaxBinaryProtocolImpl() = default;

  const std::string& name() const override { return ProtocolNames::get().LAX_BINARY; }

  bool readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) override;
  void writeMessageBegin(Buffer::Instance& buffer, const MessageMetadata& metadata) override;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
