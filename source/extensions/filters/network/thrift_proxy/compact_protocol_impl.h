#pragma once

#include <stack>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "extensions/filters/network/thrift_proxy/protocol_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * CompactProtocolImpl implements the Thrift Compact protocol.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
 */
class CompactProtocolImpl : public ProtocolImplBase {
public:
  CompactProtocolImpl(ProtocolCallbacks& callbacks) : ProtocolImplBase(callbacks) {}

  // Protocol
  const std::string& name() const override { return ProtocolNames::get().COMPACT; }
  bool readMessageBegin(Buffer::Instance& buffer, std::string& name, MessageType& msg_type,
                        int32_t& seq_id) override;
  bool readMessageEnd(Buffer::Instance& buffer) override;
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

  static bool isMagic(uint16_t word) { return (word & MagicMask) == Magic; }

private:
  // Translates compact field type IDs to FieldType.
  FieldType convertCompactFieldType(uint8_t compact_field_type);

  std::stack<int16_t> last_field_id_stack_{};
  int16_t last_field_id_{0};

  // Compact protocol encodes boolean struct fields as true/false *types* with no data.
  // This tracks the last boolean struct field's value for readBool.
  absl::optional<bool> bool_value_{};

  const static uint16_t Magic;
  const static uint16_t MagicMask;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
