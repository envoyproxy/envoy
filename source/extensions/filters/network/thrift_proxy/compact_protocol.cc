#include "extensions/filters/network/thrift_proxy/compact_protocol.h"

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

bool CompactProtocolImpl::readMessageBegin(Buffer::Instance& buffer, std::string& name,
                                           MessageType& msg_type, int32_t& seq_id) {
  // Minimum message length:
  //   protocol, message type, and version: 2 bytes +
  //   seq id (var int): 1 byte +
  //   name length (var int): 1 byte +
  //   name: 0 bytes
  if (buffer.length() < 4) {
    return false;
  }

  uint16_t version = BufferHelper::peekU16(buffer);
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
    name.assign(std::string(static_cast<char*>(buffer.linearize(name_len)), name_len));
    buffer.drain(name_len);
  } else {
    name.clear();
  }
  msg_type = type;
  seq_id = id;

  onMessageStart(absl::string_view(name), msg_type, seq_id);
  return true;
}

bool CompactProtocolImpl::readMessageEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  onMessageComplete();
  return true;
}

bool CompactProtocolImpl::readStructBegin(Buffer::Instance& buffer, std::string& name) {
  UNREFERENCED_PARAMETER(buffer);
  name.clear(); // compact protocol does not transmit struct names

  // Field ids are encoded as deltas specific to the field's containing struct. Field ids are
  // tracked in a stack to handle nested structs.
  last_field_id_stack_.push(last_field_id_);
  last_field_id_ = 0;

  onStructBegin(absl::string_view(name));
  return true;
}

bool CompactProtocolImpl::readStructEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);

  if (last_field_id_stack_.empty()) {
    throw EnvoyException("invalid check for compact protocol struct end");
  }

  last_field_id_ = last_field_id_stack_.top();
  last_field_id_stack_.pop();

  onStructEnd();
  return true;
}

bool CompactProtocolImpl::readFieldBegin(Buffer::Instance& buffer, std::string& name,
                                         FieldType& field_type, int16_t& field_id) {
  // Minimum size: FieldType::Stop is encoded as 1 byte.
  if (buffer.length() < 1) {
    return false;
  }

  uint8_t delta_and_type = BufferHelper::peekI8(buffer);
  if ((delta_and_type & 0x0f) == 0) {
    // Type is stop, no need to do further decoding.
    name.clear();
    field_id = 0;
    field_type = FieldType::Stop;
    buffer.drain(1);

    onStructField(absl::string_view(name), field_type, field_id);
    return true;
  }

  int16_t compact_field_id;
  uint8_t compact_field_type;
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

    if (id < 0 || id > INT16_MAX) {
      throw EnvoyException(fmt::format("invalid compact protocol field id {}", id));
    }

    compact_field_type = delta_and_type;
    compact_field_id = static_cast<int16_t>(id);
  } else {
    // Short form field header: 4 bits of field id delta, 4 bits of field type.
    compact_field_type = delta_and_type & 0x0F;
    compact_field_id = last_field_id_ + static_cast<int16_t>(delta_and_type >> 4);
  }

  field_type = convertCompactFieldType(compact_field_type);
  // For simple fields, boolean values are transmitted as a type with no further data.
  if (field_type == FieldType::Bool) {
    bool_value_ = compact_field_type == 1;
  }

  name.clear(); // compact protocol does not transmit field names
  field_id = compact_field_id;
  last_field_id_ = compact_field_id;

  buffer.drain(id_size + 1);

  onStructField(absl::string_view(name), field_type, field_id);
  return true;
}

FieldType CompactProtocolImpl::convertCompactFieldType(uint8_t compact_field_type) {
  switch (compact_field_type) {
  case 0:
    return FieldType::Stop;
  case 1:
    return FieldType::Bool;
  case 2:
    return FieldType::Bool;
  case 3:
    return FieldType::Byte;
  case 4:
    return FieldType::I16;
  case 5:
    return FieldType::I32;
  case 6:
    return FieldType::I64;
  case 7:
    return FieldType::Double;
  case 8:
    return FieldType::String;
  case 9:
    return FieldType::List;
  case 10:
    return FieldType::Set;
  case 11:
    return FieldType::Map;
  case 12:
    return FieldType::Struct;
  default:
    throw EnvoyException(fmt::format("unknown compact protocol field type {}", compact_field_type));
  }
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

  uint8_t types = BufferHelper::peekI8(buffer, s_size);
  FieldType ktype = convertCompactFieldType(types >> 4);
  FieldType vtype = convertCompactFieldType(types & 0xF);

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
  uint8_t size_and_type = BufferHelper::peekI8(buffer);
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

  elem_type = convertCompactFieldType(size_and_type & 0x0F);
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

  value = BufferHelper::drainI8(buffer) != 0;
  return true;
}

bool CompactProtocolImpl::readByte(Buffer::Instance& buffer, uint8_t& value) {
  if (buffer.length() < 1) {
    return false;
  }
  value = BufferHelper::drainI8(buffer);
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

  if (i < INT16_MIN || i > INT16_MAX) {
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

  value = BufferHelper::drainDouble(buffer);
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
  value.assign(static_cast<char*>(buffer.linearize(str_len)), str_len);
  buffer.drain(str_len);
  return true;
}

bool CompactProtocolImpl::readBinary(Buffer::Instance& buffer, std::string& value) {
  return readString(buffer, value);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
