#pragma once

#include <stack>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "source/extensions/filters/network/thrift_proxy/protocol.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * CompactProtocolImpl implements the Thrift Compact protocol.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
 */
class CompactProtocolImpl : public Protocol {
public:
  CompactProtocolImpl() = default;

  // Protocol
  const std::string& name() const override { return ProtocolNames::get().COMPACT; }
  ProtocolType type() const override { return ProtocolType::Compact; }
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

  static bool isMagic(uint16_t word) { return (word & MagicMask) == Magic; }

private:
  enum class CompactFieldType {
    Stop = 0,
    BoolTrue = 1,
    BoolFalse = 2,
    Byte = 3,
    I16 = 4,
    I32 = 5,
    I64 = 6,
    Double = 7,
    String = 8,
    List = 9,
    Set = 10,
    Map = 11,
    Struct = 12,
  };

  FieldType convertCompactFieldType(CompactFieldType compact_field_type);
  CompactFieldType convertFieldType(FieldType field_type);

  void writeFieldBeginInternal(Buffer::Instance& buffer, FieldType field_type, int16_t field_id,
                               absl::optional<CompactFieldType> field_type_override);

  static void validateFieldId(int32_t id);

  std::stack<int16_t> last_field_id_stack_{};
  int16_t last_field_id_{0};

  // Compact protocol encodes boolean struct fields as true/false *types* with no data.
  // This tracks the last boolean struct field's value for readBool.
  absl::optional<bool> bool_value_{};

  // Similarly, track the field id for writeBool.
  absl::optional<int16_t> bool_field_id_{};

  const static uint16_t Magic;
  const static uint16_t MagicMask;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
