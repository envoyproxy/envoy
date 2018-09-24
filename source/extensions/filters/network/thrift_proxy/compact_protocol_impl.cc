#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"

#include <limits>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

const uint16_t CompactProtocolImpl::Magic = 0x8201;
const uint16_t CompactProtocolImpl::MagicMask = 0xFF1F;

bool CompactProtocolImpl::readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) {
  // Minimum message length:
  //   protocol, message type, and version: 2 bytes +
  //   seq id (var int): 1 byte +
  //   name length (var int): 1 byte +
  //   name: 0 bytes
  if (buffer.length() < 4) {
    return false;
  }

  uint16_t version = buffer.peekBEInt<uint16_t>();
  if ((version & MagicMask) != Magic) {
    throw EnvoyException(fmt::format("invalid compact protocol version 0x{:04x} != 0x{:04x}",
                                     version & MagicMask, Magic));
  }

  MessageType type = static_cast<MessageType>((version & ~MagicMask) >> 5);
  if (type < MessageType::Call || type > MessageType::LastMessageType) {
    throw EnvoyException(
        fmt::format("invalid compact protocol message type {}", static_cast<int8_t>(type)));
  }

  int id_size;
  int32_t id = BufferHelper::peekVarIntI32(buffer, 2, id_size);
  if (id_size < 0) {
    return false;
  }

  int name_len_size;
  int32_t name_len = BufferHelper::peekVarIntI32(buffer, id_size + 2, name_len_size);
  if (name_len_size < 0) {
    return false;
  }

  if (name_len < 0) {
    throw EnvoyException(fmt::format("negative compact protocol message name length {}", name_len));
  }

  if (buffer.length() < static_cast<uint64_t>(id_size + name_len_size + name_len + 2)) {
    return false;
  }

  buffer.drain(id_size + name_len_size + 2);

  if (name_len > 0) {
    metadata.setMethodName(
        std::string(static_cast<const char*>(buffer.linearize(name_len)), name_len));
    buffer.drain(name_len);
  } else {
    metadata.setMethodName("");
  }
  metadata.setMessageType(type);
  metadata.setSequenceId(id);

  return true;
}

bool CompactProtocolImpl::readMessageEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool CompactProtocolImpl::readStructBegin(Buffer::Instance& buffer, std::string& name) {
  UNREFERENCED_PARAMETER(buffer);
  name.clear(); // compact protocol does not transmit struct names

  // Field ids are encoded as deltas specific to the field's containing struct. Field ids are
  // tracked in a stack to handle nested structs.
  last_field_id_stack_.push(last_field_id_);
  last_field_id_ = 0;

  return true;
}

bool CompactProtocolImpl::readStructEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);

  if (last_field_id_stack_.empty()) {
    throw EnvoyException("invalid check for compact protocol struct end");
  }

  last_field_id_ = last_field_id_stack_.top();
  last_field_id_stack_.pop();

  return true;
}

bool CompactProtocolImpl::readFieldBegin(Buffer::Instance& buffer, std::string& name,
                                         FieldType& field_type, int16_t& field_id) {
  // Minimum size: FieldType::Stop is encoded as 1 byte.
  if (buffer.length() < 1) {
    return false;
  }

  uint8_t delta_and_type = buffer.peekInt<int8_t>();
  if ((delta_and_type & 0x0f) == 0) {
    // Type is stop, no need to do further decoding.
    name.clear();
    field_id = 0;
    field_type = FieldType::Stop;
    buffer.drain(1);

    return true;
  }

  int16_t compact_field_id;
  CompactFieldType compact_field_type;
  int id_size = 0;
  if ((delta_and_type >> 4) == 0) {
    // Field ID delta is zero: this is a long-form field header, followed by zig-zag field id.
    if (buffer.length() < 2) {
      return false;
    }

    int32_t id = BufferHelper::peekZigZagI32(buffer, 1, id_size);
    if (id_size < 0) {
      return false;
    }

    if (id < 0 || id > std::numeric_limits<int16_t>::max()) {
      throw EnvoyException(fmt::format("invalid compact protocol field id {}", id));
    }

    compact_field_type = static_cast<CompactFieldType>(delta_and_type);
    compact_field_id = static_cast<int16_t>(id);
  } else {
    // Short form field header: 4 bits of field id delta, 4 bits of field type.
    compact_field_type = static_cast<CompactFieldType>(delta_and_type & 0x0F);
    compact_field_id = last_field_id_ + static_cast<int16_t>(delta_and_type >> 4);
  }

  field_type = convertCompactFieldType(compact_field_type);
  // For simple fields, boolean values are transmitted as a type with no further data.
  if (field_type == FieldType::Bool) {
    bool_value_ = compact_field_type == CompactFieldType::BoolTrue;
  }

  name.clear(); // compact protocol does not transmit field names
  field_id = compact_field_id;
  last_field_id_ = compact_field_id;

  buffer.drain(id_size + 1);

  return true;
}

bool CompactProtocolImpl::readFieldEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  bool_value_.reset();
  return true;
}

bool CompactProtocolImpl::readMapBegin(Buffer::Instance& buffer, FieldType& key_type,
                                       FieldType& value_type, uint32_t& size) {
  int s_size;
  int32_t s = BufferHelper::peekVarIntI32(buffer, 0, s_size);
  if (s_size < 0) {
    return false;
  }

  if (s < 0) {
    throw EnvoyException(fmt::format("negative compact protocol map size {}", s));
  }

  if (s == 0) {
    // Empty map. Compact protocol provides no type information in this case.
    key_type = value_type = FieldType::Stop;
    size = 0;
    buffer.drain(s_size);
    return true;
  }

  if (buffer.length() < static_cast<uint64_t>(s_size + 1)) {
    return false;
  }

  uint8_t types = buffer.peekInt<int8_t>(s_size);
  FieldType ktype = convertCompactFieldType(static_cast<CompactFieldType>(types >> 4));
  FieldType vtype = convertCompactFieldType(static_cast<CompactFieldType>(types & 0xF));

  // Drain the size and the types byte.
  buffer.drain(s_size + 1);

  key_type = ktype;
  value_type = vtype;
  size = static_cast<uint32_t>(s);

  return true;
}

bool CompactProtocolImpl::readMapEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool CompactProtocolImpl::readListBegin(Buffer::Instance& buffer, FieldType& elem_type,
                                        uint32_t& size) {
  // Minimum length:
  //   size and type: 1 byte
  if (buffer.length() < 1) {
    return false;
  }

  uint32_t sz = 0;
  int s_size = 0;
  uint8_t size_and_type = buffer.peekInt<int8_t>();
  if ((size_and_type & 0xF0) != 0xF0) {
    // Short form list header: size and type byte.
    sz = static_cast<uint32_t>(size_and_type >> 4);
  } else {
    // Long form list header: type byte followed by var int size.
    int32_t s = BufferHelper::peekVarIntI32(buffer, 1, s_size);
    if (s_size < 0) {
      return false;
    }

    if (s < 0) {
      throw EnvoyException(fmt::format("negative compact procotol list/set size {}", s));
    }

    sz = static_cast<uint32_t>(s);
  }

  elem_type = convertCompactFieldType(static_cast<CompactFieldType>(size_and_type & 0x0F));
  size = sz;

  buffer.drain(s_size + 1);
  return true;
}

bool CompactProtocolImpl::readListEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool CompactProtocolImpl::readSetBegin(Buffer::Instance& buffer, FieldType& elem_type,
                                       uint32_t& size) {
  return readListBegin(buffer, elem_type, size);
}

bool CompactProtocolImpl::readSetEnd(Buffer::Instance& buffer) { return readListEnd(buffer); }

bool CompactProtocolImpl::readBool(Buffer::Instance& buffer, bool& value) {
  // Boolean struct fields have their value encoded in the field type.
  if (bool_value_.has_value()) {
    value = bool_value_.value();
    return true;
  }

  // All other boolean values (list, set, or map elements) are encoded as single bytes.
  if (buffer.length() < 1) {
    return false;
  }

  value = buffer.drainInt<int8_t>() != 0;
  return true;
}

bool CompactProtocolImpl::readByte(Buffer::Instance& buffer, uint8_t& value) {
  if (buffer.length() < 1) {
    return false;
  }
  value = buffer.drainInt<int8_t>();
  return true;
}

bool CompactProtocolImpl::readInt16(Buffer::Instance& buffer, int16_t& value) {
  if (buffer.length() < 1) {
    return false;
  }

  int size;
  int32_t i = BufferHelper::peekZigZagI32(buffer, 0, size);
  if (size < 0) {
    return false;
  }

  if (i < std::numeric_limits<int16_t>::min() || i > std::numeric_limits<int16_t>::max()) {
    throw EnvoyException(fmt::format("compact protocol i16 exceeds allowable range {}", i));
  }

  buffer.drain(size);
  value = static_cast<int16_t>(i);
  return true;
}

bool CompactProtocolImpl::readInt32(Buffer::Instance& buffer, int32_t& value) {
  if (buffer.length() < 1) {
    return false;
  }

  int size;
  int32_t i = BufferHelper::peekZigZagI32(buffer, 0, size);
  if (size < 0) {
    return false;
  }

  buffer.drain(size);
  value = i;
  return true;
}

bool CompactProtocolImpl::readInt64(Buffer::Instance& buffer, int64_t& value) {
  if (buffer.length() < 1) {
    return false;
  }

  int size;
  int64_t i = BufferHelper::peekZigZagI64(buffer, 0, size);
  if (size < 0) {
    return false;
  }

  buffer.drain(size);
  value = i;
  return true;
}

bool CompactProtocolImpl::readDouble(Buffer::Instance& buffer, double& value) {
  static_assert(sizeof(double) == sizeof(uint64_t), "sizeof(double) != size(uint64_t)");

  if (buffer.length() < 8) {
    return false;
  }

  value = BufferHelper::drainBEDouble(buffer);
  return true;
}

bool CompactProtocolImpl::readString(Buffer::Instance& buffer, std::string& value) {
  if (buffer.length() < 1) {
    return false;
  }

  int len_size;
  int32_t str_len = BufferHelper::peekVarIntI32(buffer, 0, len_size);
  if (len_size < 0) {
    return false;
  }

  if (str_len < 0) {
    throw EnvoyException(fmt::format("negative compact protocol string/binary length {}", str_len));
  }

  if (str_len == 0) {
    buffer.drain(len_size);
    value.clear();
    return true;
  }

  if (buffer.length() < static_cast<uint64_t>(str_len + len_size)) {
    return false;
  }

  buffer.drain(len_size);
  value.assign(static_cast<const char*>(buffer.linearize(str_len)), str_len);
  buffer.drain(str_len);
  return true;
}

bool CompactProtocolImpl::readBinary(Buffer::Instance& buffer, std::string& value) {
  return readString(buffer, value);
}

void CompactProtocolImpl::writeMessageBegin(Buffer::Instance& buffer,
                                            const MessageMetadata& metadata) {
  MessageType msg_type = metadata.messageType();

  uint16_t ptv = (Magic & MagicMask) | (static_cast<uint16_t>(msg_type) << 5);
  ASSERT((ptv & MagicMask) == Magic);
  ASSERT((ptv & ~MagicMask) >> 5 == static_cast<uint16_t>(msg_type));

  buffer.writeBEInt<uint16_t>(ptv);
  BufferHelper::writeVarIntI32(buffer, metadata.sequenceId());
  writeString(buffer, metadata.methodName());
}

void CompactProtocolImpl::writeMessageEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
}

void CompactProtocolImpl::writeStructBegin(Buffer::Instance& buffer, const std::string& name) {
  UNREFERENCED_PARAMETER(buffer);
  UNREFERENCED_PARAMETER(name);

  // Field ids are encoded as deltas specific to the field's containing struct. Field ids are
  // tracked in a stack to handle nested structs.
  last_field_id_stack_.push(last_field_id_);
  last_field_id_ = 0;
}

void CompactProtocolImpl::writeStructEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);

  if (last_field_id_stack_.empty()) {
    throw EnvoyException("invalid write of compact protocol struct end");
  }

  last_field_id_ = last_field_id_stack_.top();
  last_field_id_stack_.pop();
}

void CompactProtocolImpl::writeFieldBegin(Buffer::Instance& buffer, const std::string& name,
                                          FieldType field_type, int16_t field_id) {
  UNREFERENCED_PARAMETER(name);

  if (field_type == FieldType::Stop) {
    buffer.writeByte(0);
    return;
  }

  if (field_type == FieldType::Bool) {
    bool_field_id_ = field_id;
    return;
  }

  writeFieldBeginInternal(buffer, field_type, field_id, {});
}

void CompactProtocolImpl::writeFieldBeginInternal(
    Buffer::Instance& buffer, FieldType field_type, int16_t field_id,
    absl::optional<CompactFieldType> field_type_override) {
  CompactFieldType compact_field_type;
  if (field_type_override.has_value()) {
    compact_field_type = field_type_override.value();
  } else {
    compact_field_type = convertFieldType(field_type);
  }

  if (field_id > last_field_id_ && field_id - last_field_id_ <= 15) {
    // Encode short-form field header.
    buffer.writeByte((static_cast<int8_t>(field_id - last_field_id_) << 4) |
                     static_cast<int8_t>(compact_field_type));
  } else {
    buffer.writeByte(static_cast<int8_t>(compact_field_type));
    BufferHelper::writeZigZagI32(buffer, static_cast<int32_t>(field_id));
  }

  last_field_id_ = field_id;
}

void CompactProtocolImpl::writeFieldEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);

  bool_field_id_.reset();
}

void CompactProtocolImpl::writeMapBegin(Buffer::Instance& buffer, FieldType key_type,
                                        FieldType value_type, uint32_t size) {
  if (size > std::numeric_limits<int32_t>::max()) {
    throw EnvoyException(fmt::format("illegal compact protocol map size {}", size));
  }

  BufferHelper::writeVarIntI32(buffer, static_cast<int32_t>(size));
  if (size == 0) {
    return;
  }

  CompactFieldType compact_key_type = convertFieldType(key_type);
  CompactFieldType compact_value_type = convertFieldType(value_type);
  buffer.writeByte((static_cast<int8_t>(compact_key_type) << 4) |
                   static_cast<int8_t>(compact_value_type));
}

void CompactProtocolImpl::writeMapEnd(Buffer::Instance& buffer) { UNREFERENCED_PARAMETER(buffer); }

void CompactProtocolImpl::writeListBegin(Buffer::Instance& buffer, FieldType elem_type,
                                         uint32_t size) {
  if (size > std::numeric_limits<int32_t>::max()) {
    throw EnvoyException(fmt::format("illegal compact protocol list/set size {}", size));
  }

  CompactFieldType compact_elem_type = convertFieldType(elem_type);

  if (size < 0xF) {
    // Short form list/set header
    int8_t short_size = static_cast<int8_t>(size & 0xF);
    buffer.writeByte((short_size << 4) | static_cast<int8_t>(compact_elem_type));
  } else {
    buffer.writeByte(0xF0 | static_cast<int8_t>(compact_elem_type));
    BufferHelper::writeVarIntI32(buffer, static_cast<int32_t>(size));
  }
}

void CompactProtocolImpl::writeListEnd(Buffer::Instance& buffer) { UNREFERENCED_PARAMETER(buffer); }

void CompactProtocolImpl::writeSetBegin(Buffer::Instance& buffer, FieldType elem_type,
                                        uint32_t size) {
  writeListBegin(buffer, elem_type, size);
}

void CompactProtocolImpl::writeSetEnd(Buffer::Instance& buffer) { UNREFERENCED_PARAMETER(buffer); }

void CompactProtocolImpl::writeBool(Buffer::Instance& buffer, bool value) {
  if (bool_field_id_.has_value()) {
    // Boolean fields have their value encoded by type.
    CompactFieldType bool_field_type =
        value ? CompactFieldType::BoolTrue : CompactFieldType::BoolFalse;
    writeFieldBeginInternal(buffer, FieldType::Bool, bool_field_id_.value(), {bool_field_type});
    return;
  }

  // Map/Set/List booleans are encoded as bytes.
  buffer.writeByte(value ? 1 : 0);
}

void CompactProtocolImpl::writeByte(Buffer::Instance& buffer, uint8_t value) {
  buffer.writeByte(value);
}

void CompactProtocolImpl::writeInt16(Buffer::Instance& buffer, int16_t value) {
  int32_t extended = static_cast<int32_t>(value);
  BufferHelper::writeZigZagI32(buffer, extended);
}

void CompactProtocolImpl::writeInt32(Buffer::Instance& buffer, int32_t value) {
  BufferHelper::writeZigZagI32(buffer, value);
}

void CompactProtocolImpl::writeInt64(Buffer::Instance& buffer, int64_t value) {
  BufferHelper::writeZigZagI64(buffer, value);
}

void CompactProtocolImpl::writeDouble(Buffer::Instance& buffer, double value) {
  BufferHelper::writeBEDouble(buffer, value);
}

void CompactProtocolImpl::writeString(Buffer::Instance& buffer, const std::string& value) {
  BufferHelper::writeVarIntI32(buffer, value.length());
  buffer.add(value);
}

void CompactProtocolImpl::writeBinary(Buffer::Instance& buffer, const std::string& value) {
  writeString(buffer, value);
}

FieldType CompactProtocolImpl::convertCompactFieldType(CompactFieldType compact_field_type) {
  switch (compact_field_type) {
  case CompactFieldType::BoolTrue:
    return FieldType::Bool;
  case CompactFieldType::BoolFalse:
    return FieldType::Bool;
  case CompactFieldType::Byte:
    return FieldType::Byte;
  case CompactFieldType::I16:
    return FieldType::I16;
  case CompactFieldType::I32:
    return FieldType::I32;
  case CompactFieldType::I64:
    return FieldType::I64;
  case CompactFieldType::Double:
    return FieldType::Double;
  case CompactFieldType::String:
    return FieldType::String;
  case CompactFieldType::List:
    return FieldType::List;
  case CompactFieldType::Set:
    return FieldType::Set;
  case CompactFieldType::Map:
    return FieldType::Map;
  case CompactFieldType::Struct:
    return FieldType::Struct;
  default:
    throw EnvoyException(fmt::format("unknown compact protocol field type {}",
                                     static_cast<int8_t>(compact_field_type)));
  }
}

CompactProtocolImpl::CompactFieldType CompactProtocolImpl::convertFieldType(FieldType field_type) {
  switch (field_type) {
  case FieldType::Bool:
    // c.f. special handling in writeFieldBegin
    return CompactFieldType::BoolTrue;
  case FieldType::Byte:
    return CompactFieldType::Byte;
  case FieldType::I16:
    return CompactFieldType::I16;
  case FieldType::I32:
    return CompactFieldType::I32;
  case FieldType::I64:
    return CompactFieldType::I64;
  case FieldType::Double:
    return CompactFieldType::Double;
  case FieldType::String:
    return CompactFieldType::String;
  case FieldType::Struct:
    return CompactFieldType::Struct;
  case FieldType::Map:
    return CompactFieldType::Map;
  case FieldType::Set:
    return CompactFieldType::Set;
  case FieldType::List:
    return CompactFieldType::List;
  default:
    throw EnvoyException(
        fmt::format("unknown protocol field type {}", static_cast<int8_t>(field_type)));
  }
}

class CompactProtocolConfigFactory : public ProtocolFactoryBase<CompactProtocolImpl> {
public:
  CompactProtocolConfigFactory() : ProtocolFactoryBase(ProtocolNames::get().COMPACT) {}
};

/**
 * Static registration for the binary protocol. @see RegisterFactory.
 */
static Registry::RegisterFactory<CompactProtocolConfigFactory, NamedProtocolConfigFactory>
    register_;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
