#include "extensions/filters/network/thrift_proxy/binary_protocol_impl.h"

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

const uint16_t BinaryProtocolImpl::Magic = 0x8001;

bool BinaryProtocolImpl::readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) {
  if (buffer.length() < MinMessageBeginLength) {
    return false;
  }

  uint16_t version = buffer.peekBEInt<uint16_t>();
  if (version != Magic) {
    throw EnvoyException(
        fmt::format("invalid binary protocol version 0x{:04x} != 0x{:04x}", version, Magic));
  }

  // The byte at offset 2 is unused and ignored.

  MessageType type = static_cast<MessageType>(buffer.peekInt<int8_t>(3));
  if (type < MessageType::Call || type > MessageType::LastMessageType) {
    throw EnvoyException(
        fmt::format("invalid binary protocol message type {}", static_cast<int8_t>(type)));
  }

  uint32_t name_len = buffer.peekBEInt<uint32_t>(4);
  if (buffer.length() < name_len + MinMessageBeginLength) {
    return false;
  }

  buffer.drain(8);

  if (name_len > 0) {
    metadata.setMethodName(
        std::string(static_cast<const char*>(buffer.linearize(name_len)), name_len));
    buffer.drain(name_len);
  } else {
    metadata.setMethodName("");
  }
  metadata.setMessageType(type);
  metadata.setSequenceId(buffer.drainBEInt<int32_t>());

  return true;
}

bool BinaryProtocolImpl::readMessageEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool BinaryProtocolImpl::readStructBegin(Buffer::Instance& buffer, std::string& name) {
  UNREFERENCED_PARAMETER(buffer);
  name.clear(); // binary protocol does not transmit struct names
  return true;
}

bool BinaryProtocolImpl::readStructEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool BinaryProtocolImpl::readFieldBegin(Buffer::Instance& buffer, std::string& name,
                                        FieldType& field_type, int16_t& field_id) {
  // FieldType::Stop is encoded as 1 byte.
  if (buffer.length() < 1) {
    return false;
  }

  FieldType type = static_cast<FieldType>(buffer.peekInt<int8_t>());
  if (type == FieldType::Stop) {
    field_id = 0;
    buffer.drain(1);
  } else {
    // FieldType followed by 2 bytes of field id
    if (buffer.length() < 3) {
      return false;
    }
    int16_t id = buffer.peekBEInt<int16_t>(1);
    if (id < 0) {
      throw EnvoyException(absl::StrCat("invalid binary protocol field id ", id));
    }
    field_id = id;
    buffer.drain(3);
  }

  name.clear(); // binary protocol does not transmit field names
  field_type = type;

  return true;
}

bool BinaryProtocolImpl::readFieldEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool BinaryProtocolImpl::readMapBegin(Buffer::Instance& buffer, FieldType& key_type,
                                      FieldType& value_type, uint32_t& size) {
  // Minimum length:
  //   key type: 1 byte +
  //   value type: 1 byte +
  //   map size: 4 bytes
  if (buffer.length() < 6) {
    return false;
  }

  FieldType ktype = static_cast<FieldType>(buffer.peekInt<int8_t>(0));
  FieldType vtype = static_cast<FieldType>(buffer.peekInt<int8_t>(1));
  int32_t s = buffer.peekBEInt<int32_t>(2);
  if (s < 0) {
    throw EnvoyException(absl::StrCat("negative binary protocol map size ", s));
  }

  buffer.drain(6);

  key_type = ktype;
  value_type = vtype;
  size = static_cast<uint32_t>(s);

  return true;
}

bool BinaryProtocolImpl::readMapEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool BinaryProtocolImpl::readListBegin(Buffer::Instance& buffer, FieldType& elem_type,
                                       uint32_t& size) {
  // Minimum length:
  //   elem type: 1 byte +
  //   map size: 4 bytes
  if (buffer.length() < 5) {
    return false;
  }

  FieldType type = static_cast<FieldType>(buffer.peekInt<int8_t>());
  int32_t s = buffer.peekBEInt<int32_t>(1);
  if (s < 0) {
    throw EnvoyException(fmt::format("negative binary protocol list/set size {}", s));
  }
  buffer.drain(5);

  elem_type = type;
  size = static_cast<uint32_t>(s);

  return true;
}

bool BinaryProtocolImpl::readListEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
  return true;
}

bool BinaryProtocolImpl::readSetBegin(Buffer::Instance& buffer, FieldType& elem_type,
                                      uint32_t& size) {
  return readListBegin(buffer, elem_type, size);
}

bool BinaryProtocolImpl::readSetEnd(Buffer::Instance& buffer) { return readListEnd(buffer); }

bool BinaryProtocolImpl::readBool(Buffer::Instance& buffer, bool& value) {
  if (buffer.length() < 1) {
    return false;
  }

  value = buffer.drainInt<int8_t>() != 0;
  return true;
}

bool BinaryProtocolImpl::readByte(Buffer::Instance& buffer, uint8_t& value) {
  if (buffer.length() < 1) {
    return false;
  }
  value = buffer.drainInt<int8_t>();
  return true;
}

bool BinaryProtocolImpl::readInt16(Buffer::Instance& buffer, int16_t& value) {
  if (buffer.length() < 2) {
    return false;
  }
  value = buffer.drainBEInt<int16_t>();
  return true;
}

bool BinaryProtocolImpl::readInt32(Buffer::Instance& buffer, int32_t& value) {
  if (buffer.length() < 4) {
    return false;
  }
  value = buffer.drainBEInt<int32_t>();
  return true;
}

bool BinaryProtocolImpl::readInt64(Buffer::Instance& buffer, int64_t& value) {
  if (buffer.length() < 8) {
    return false;
  }
  value = buffer.drainBEInt<int64_t>();
  return true;
}

bool BinaryProtocolImpl::readDouble(Buffer::Instance& buffer, double& value) {
  static_assert(sizeof(double) == sizeof(uint64_t), "sizeof(double) != size(uint64_t)");

  if (buffer.length() < 8) {
    return false;
  }

  value = BufferHelper::drainBEDouble(buffer);
  return true;
}

bool BinaryProtocolImpl::readString(Buffer::Instance& buffer, std::string& value) {
  // Encoded as size (4 bytes) followed by string (0+ bytes).
  if (buffer.length() < 4) {
    return false;
  }

  int32_t str_len = buffer.peekBEInt<int32_t>();
  if (str_len < 0) {
    throw EnvoyException(fmt::format("negative binary protocol string/binary length {}", str_len));
  }

  if (str_len == 0) {
    buffer.drain(4);
    value.clear();
    return true;
  }

  if (buffer.length() < static_cast<uint64_t>(str_len) + 4) {
    return false;
  }

  buffer.drain(4);
  value.assign(static_cast<const char*>(buffer.linearize(str_len)), str_len);
  buffer.drain(str_len);
  return true;
}

bool BinaryProtocolImpl::readBinary(Buffer::Instance& buffer, std::string& value) {
  return readString(buffer, value);
}

void BinaryProtocolImpl::writeMessageBegin(Buffer::Instance& buffer,
                                           const MessageMetadata& metadata) {
  buffer.writeBEInt<uint16_t>(Magic);
  buffer.writeBEInt<uint16_t>(static_cast<uint16_t>(metadata.messageType()));
  writeString(buffer, metadata.methodName());
  buffer.writeBEInt<int32_t>(metadata.sequenceId());
}

void BinaryProtocolImpl::writeMessageEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
}

void BinaryProtocolImpl::writeStructBegin(Buffer::Instance& buffer, const std::string& name) {
  UNREFERENCED_PARAMETER(buffer);
  UNREFERENCED_PARAMETER(name);
}

void BinaryProtocolImpl::writeStructEnd(Buffer::Instance& buffer) {
  UNREFERENCED_PARAMETER(buffer);
}

void BinaryProtocolImpl::writeFieldBegin(Buffer::Instance& buffer, const std::string& name,
                                         FieldType field_type, int16_t field_id) {
  UNREFERENCED_PARAMETER(name);

  buffer.writeByte(static_cast<uint8_t>(field_type));
  if (field_type == FieldType::Stop) {
    return;
  }

  buffer.writeBEInt<int16_t>(field_id);
}

void BinaryProtocolImpl::writeFieldEnd(Buffer::Instance& buffer) { UNREFERENCED_PARAMETER(buffer); }

void BinaryProtocolImpl::writeMapBegin(Buffer::Instance& buffer, FieldType key_type,
                                       FieldType value_type, uint32_t size) {
  if (size > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
    throw EnvoyException(absl::StrCat("illegal binary protocol map size ", size));
  }

  buffer.writeByte(static_cast<int8_t>(key_type));
  buffer.writeByte(static_cast<int8_t>(value_type));
  buffer.writeBEInt<int32_t>(static_cast<int32_t>(size));
}

void BinaryProtocolImpl::writeMapEnd(Buffer::Instance& buffer) { UNREFERENCED_PARAMETER(buffer); }

void BinaryProtocolImpl::writeListBegin(Buffer::Instance& buffer, FieldType elem_type,
                                        uint32_t size) {
  if (size > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
    throw EnvoyException(fmt::format("illegal binary protocol list/set size {}", size));
  }

  buffer.writeByte(static_cast<int8_t>(elem_type));
  buffer.writeBEInt<int32_t>(static_cast<int32_t>(size));
}

void BinaryProtocolImpl::writeListEnd(Buffer::Instance& buffer) { UNREFERENCED_PARAMETER(buffer); }

void BinaryProtocolImpl::writeSetBegin(Buffer::Instance& buffer, FieldType elem_type,
                                       uint32_t size) {
  writeListBegin(buffer, elem_type, size);
}

void BinaryProtocolImpl::writeSetEnd(Buffer::Instance& buffer) { writeListEnd(buffer); }

void BinaryProtocolImpl::writeBool(Buffer::Instance& buffer, bool value) {
  buffer.writeByte(value ? 1 : 0);
}

void BinaryProtocolImpl::writeByte(Buffer::Instance& buffer, uint8_t value) {
  buffer.writeByte(value);
}

void BinaryProtocolImpl::writeInt16(Buffer::Instance& buffer, int16_t value) {
  buffer.writeBEInt<int16_t>(value);
}

void BinaryProtocolImpl::writeInt32(Buffer::Instance& buffer, int32_t value) {
  buffer.writeBEInt<int32_t>(value);
}

void BinaryProtocolImpl::writeInt64(Buffer::Instance& buffer, int64_t value) {
  buffer.writeBEInt<int64_t>(value);
}

void BinaryProtocolImpl::writeDouble(Buffer::Instance& buffer, double value) {
  BufferHelper::writeBEDouble(buffer, value);
}

void BinaryProtocolImpl::writeString(Buffer::Instance& buffer, const std::string& value) {
  buffer.writeBEInt<uint32_t>(value.length());
  buffer.add(value);
}

void BinaryProtocolImpl::writeBinary(Buffer::Instance& buffer, const std::string& value) {
  writeString(buffer, value);
}

bool LaxBinaryProtocolImpl::readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) {
  // Minimum message length:
  //   name len: 4 bytes +
  //   name: 0 bytes +
  //   msg type: 1 byte +
  //   seq id: 4 bytes
  if (buffer.length() < 9) {
    return false;
  }

  uint32_t name_len = buffer.peekBEInt<uint32_t>();

  if (buffer.length() < 9 + name_len) {
    return false;
  }

  MessageType type = static_cast<MessageType>(buffer.peekInt<int8_t>(name_len + 4));
  if (type < MessageType::Call || type > MessageType::LastMessageType) {
    throw EnvoyException(
        fmt::format("invalid (lax) binary protocol message type {}", static_cast<int8_t>(type)));
  }

  buffer.drain(4);
  if (name_len > 0) {
    metadata.setMethodName(
        std::string(static_cast<const char*>(buffer.linearize(name_len)), name_len));
    buffer.drain(name_len);
  } else {
    metadata.setMethodName("");
  }

  metadata.setMessageType(type);
  metadata.setSequenceId(buffer.peekBEInt<int32_t>(1));
  buffer.drain(5);

  return true;
}

void LaxBinaryProtocolImpl::writeMessageBegin(Buffer::Instance& buffer,
                                              const MessageMetadata& metadata) {
  writeString(buffer, metadata.methodName());
  buffer.writeByte(static_cast<int8_t>(metadata.messageType()));
  buffer.writeBEInt<int32_t>(metadata.sequenceId());
}

class BinaryProtocolConfigFactory : public ProtocolFactoryBase<BinaryProtocolImpl> {
public:
  BinaryProtocolConfigFactory() : ProtocolFactoryBase(ProtocolNames::get().BINARY) {}
};

/**
 * Static registration for the binary protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(BinaryProtocolConfigFactory, NamedProtocolConfigFactory);

class LaxBinaryProtocolConfigFactory : public ProtocolFactoryBase<LaxBinaryProtocolImpl> {
public:
  LaxBinaryProtocolConfigFactory() : ProtocolFactoryBase(ProtocolNames::get().LAX_BINARY) {}
};

/**
 * Static registration for the auto protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(LaxBinaryProtocolConfigFactory, NamedProtocolConfigFactory);

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
