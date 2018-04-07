#include "extensions/filters/network/mongo_proxy/bson_impl.h"

#include <cstdint>
#include <sstream>
#include <string>

#include "common/common/assert.h"
#include "common/common/byte_order.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {
namespace Bson {

int32_t BufferHelper::peakInt32(Buffer::Instance& data) {
  if (data.length() < sizeof(int32_t)) {
    throw EnvoyException("invalid buffer size");
  }

  int32_t val;
  void* mem = data.linearize(sizeof(int32_t));
  std::memcpy(reinterpret_cast<void*>(&val), mem, sizeof(int32_t));
  return le32toh(val);
}

uint8_t BufferHelper::removeByte(Buffer::Instance& data) {
  if (data.length() == 0) {
    throw EnvoyException("invalid buffer size");
  }

  void* mem = data.linearize(sizeof(uint8_t));
  uint8_t ret = *reinterpret_cast<uint8_t*>(mem);
  data.drain(sizeof(uint8_t));
  return ret;
}

void BufferHelper::removeBytes(Buffer::Instance& data, uint8_t* out, size_t out_len) {
  if (data.length() < out_len) {
    throw EnvoyException("invalid buffer size");
  }

  void* mem = data.linearize(out_len);
  std::memcpy(out, mem, out_len);
  data.drain(out_len);
}

std::string BufferHelper::removeCString(Buffer::Instance& data) {
  char end = '\0';
  ssize_t index = data.search(&end, sizeof(end), 0);
  if (index == -1) {
    throw EnvoyException("invalid CString");
  }

  char* start = reinterpret_cast<char*>(data.linearize(index + 1));
  std::string ret(start);
  data.drain(index + 1);
  return ret;
}

double BufferHelper::removeDouble(Buffer::Instance& data) {
  ASSERT(sizeof(double) == 8);

  // There is not really official endian support for floating point so we unpack an 8 byte integer
  // into a union with a double.
  union {
    int64_t i;
    double d;
  } memory;

  static_assert(sizeof(memory.i) == sizeof(memory.d), "invalid type size");
  memory.i = removeInt64(data);
  return memory.d;
}

int32_t BufferHelper::removeInt32(Buffer::Instance& data) {
  int32_t ret = peakInt32(data);
  data.drain(sizeof(int32_t));
  return ret;
}

int64_t BufferHelper::removeInt64(Buffer::Instance& data) {
  if (data.length() < sizeof(int64_t)) {
    throw EnvoyException("invalid buffer size");
  }

  int64_t val;
  void* mem = data.linearize(sizeof(int64_t));
  std::memcpy(reinterpret_cast<void*>(&val), mem, sizeof(int64_t));
  data.drain(sizeof(int64_t));
  return le64toh(val);
}

std::string BufferHelper::removeString(Buffer::Instance& data) {
  int32_t length = removeInt32(data);
  char* start = reinterpret_cast<char*>(data.linearize(length));
  std::string ret(start);
  data.drain(length);
  return ret;
}

std::string BufferHelper::removeBinary(Buffer::Instance& data) {
  // Read out the subtype but do not store it for now.
  int32_t length = removeInt32(data);
  removeByte(data);
  char* start = reinterpret_cast<char*>(data.linearize(length));
  std::string ret(start, length);
  data.drain(length);
  return ret;
}

void BufferHelper::writeCString(Buffer::Instance& data, const std::string& value) {
  data.add(value.c_str(), value.size() + 1);
}

void BufferHelper::writeDouble(Buffer::Instance& data, double value) {
  // We need to hack converting a double into little endian.
  int64_t* to_write = reinterpret_cast<int64_t*>(&value);
  writeInt64(data, *to_write);
}

void BufferHelper::writeInt32(Buffer::Instance& data, int32_t value) {
  value = htole32(value);
  data.add(&value, sizeof(value));
}

void BufferHelper::writeInt64(Buffer::Instance& data, int64_t value) {
  value = htole64(value);
  data.add(&value, sizeof(value));
}

void BufferHelper::writeString(Buffer::Instance& data, const std::string& value) {
  writeInt32(data, value.size() + 1);
  data.add(value.c_str(), value.size() + 1);
}

void BufferHelper::writeBinary(Buffer::Instance& data, const std::string& value) {
  // Right now we do not actually store the binary subtype and always use zero.
  writeInt32(data, value.size());
  uint8_t subtype = 0;
  data.add(&subtype, sizeof(subtype));
  data.add(value.c_str(), value.size());
}

int32_t FieldImpl::byteSize() const {
  // 1 byte type, cstring key, field.
  int32_t total = 1 + key_.size() + 1;

  switch (type_) {
  case Type::DOUBLE:
  case Type::DATETIME:
  case Type::TIMESTAMP:
  case Type::INT64: {
    return total + 8;
  }

  case Type::STRING: {
    return total + 4 + value_.string_value_.size() + 1;
  }

  case Type::DOCUMENT:
  case Type::ARRAY: {
    return total + value_.document_value_->byteSize();
  }

  case Type::BINARY: {
    return total + 5 + value_.string_value_.size();
  }

  case Type::OBJECT_ID: {
    return total + sizeof(ObjectId);
  }

  case Type::BOOLEAN: {
    return total + 1;
  }

  case Type::NULL_VALUE: {
    return total;
  }

  case Type::REGEX: {
    return total + value_.regex_value_.pattern_.size() + value_.regex_value_.options_.size() + 2;
  }

  case Type::INT32: {
    return total + 4;
  }
  }

  NOT_REACHED;
}

void FieldImpl::encode(Buffer::Instance& output) const {
  output.add(&type_, sizeof(type_));
  BufferHelper::writeCString(output, key_);

  switch (type_) {
  case Type::DOUBLE: {
    return BufferHelper::writeDouble(output, value_.double_value_);
  }

  case Type::STRING: {
    return BufferHelper::writeString(output, value_.string_value_);
  }

  case Type::DOCUMENT:
  case Type::ARRAY: {
    return value_.document_value_->encode(output);
  }

  case Type::BINARY: {
    return BufferHelper::writeBinary(output, value_.string_value_);
  }

  case Type::OBJECT_ID: {
    return output.add(&value_.object_id_value_[0], value_.object_id_value_.size());
  }

  case Type::BOOLEAN: {
    uint8_t to_write = value_.bool_value_ ? 1 : 0;
    return output.add(&to_write, sizeof(to_write));
  }

  case Type::DATETIME:
  case Type::TIMESTAMP:
  case Type::INT64: {
    return BufferHelper::writeInt64(output, value_.int64_value_);
  }

  case Type::NULL_VALUE: {
    return;
  }

  case Type::REGEX: {
    BufferHelper::writeCString(output, value_.regex_value_.pattern_);
    return BufferHelper::writeCString(output, value_.regex_value_.options_);
  }

  case Type::INT32:
    return BufferHelper::writeInt32(output, value_.int32_value_);
  }

  NOT_REACHED;
}

bool FieldImpl::operator==(const Field& rhs) const {
  if (type() != rhs.type()) {
    return false;
  }

  switch (type_) {
  case Type::DOUBLE: {
    return asDouble() == rhs.asDouble();
  }

  case Type::STRING: {
    return asString() == rhs.asString();
  }

  case Type::DOCUMENT: {
    return asDocument() == rhs.asDocument();
  }

  case Type::ARRAY: {
    return asArray() == rhs.asArray();
  }

  case Type::BINARY: {
    return asBinary() == rhs.asBinary();
  }

  case Type::OBJECT_ID: {
    return asObjectId() == rhs.asObjectId();
  }

  case Type::BOOLEAN: {
    return asBoolean() == rhs.asBoolean();
  }

  case Type::NULL_VALUE: {
    return true;
  }

  case Type::REGEX: {
    return asRegex() == rhs.asRegex();
  }

  case Type::INT32: {
    return asInt32() == rhs.asInt32();
  }

  case Type::DATETIME: {
    return asDatetime() == rhs.asDatetime();
  }

  case Type::TIMESTAMP: {
    return asTimestamp() == rhs.asTimestamp();
  }

  case Type::INT64: {
    return asInt64() == rhs.asInt64();
  }
  }

  NOT_REACHED;
}

std::string FieldImpl::toString() const {
  switch (type_) {
  case Type::DOUBLE: {
    return std::to_string(value_.double_value_);
  }

  case Type::STRING:
  case Type::BINARY: {
    return fmt::format("\"{}\"", StringUtil::escape(value_.string_value_));
  }

  case Type::DOCUMENT:
  case Type::ARRAY: {
    return value_.document_value_->toString();
  }

  case Type::OBJECT_ID: {
    return fmt::format("\"{}\"",
                       Hex::encode(&value_.object_id_value_[0], value_.object_id_value_.size()));
  }

  case Type::BOOLEAN: {
    return value_.bool_value_ ? "true" : "false";
  }

  case Type::NULL_VALUE: {
    return "null";
  }

  case Type::REGEX: {
    return fmt::format("[\"{}\", \"{}\"]", value_.regex_value_.pattern_,
                       value_.regex_value_.options_);
  }

  case Type::INT32: {
    return std::to_string(value_.int32_value_);
  }

  case Type::DATETIME:
  case Type::TIMESTAMP:
  case Type::INT64: {
    return std::to_string(value_.int64_value_);
  }
  }

  NOT_REACHED;
}

void DocumentImpl::fromBuffer(Buffer::Instance& data) {
  uint64_t original_buffer_length = data.length();
  int32_t message_length = BufferHelper::removeInt32(data);
  if (static_cast<uint64_t>(message_length) > original_buffer_length) {
    throw EnvoyException("invalid BSON message length");
  }

  ENVOY_LOG(trace, "BSON document length: {} data length: {}", message_length,
            original_buffer_length);

  while (true) {
    uint64_t document_bytes_remaining = data.length() - (original_buffer_length - message_length);
    ENVOY_LOG(trace, "BSON document bytes remaining: {}", document_bytes_remaining);
    if (document_bytes_remaining == 1) {
      uint8_t last_byte = BufferHelper::removeByte(data);
      if (last_byte != 0) {
        throw EnvoyException("invalid document");
      }

      return;
    }

    uint8_t element_type = BufferHelper::removeByte(data);
    std::string key = BufferHelper::removeCString(data);
    ENVOY_LOG(trace, "BSON element type: {:#x} key: {}", element_type, key);
    switch (static_cast<Field::Type>(element_type)) {
    case Field::Type::DOUBLE: {
      double value = BufferHelper::removeDouble(data);
      ENVOY_LOG(trace, "BSON double: {}", value);
      addDouble(key, value);
      break;
    }

    case Field::Type::STRING: {
      std::string value = BufferHelper::removeString(data);
      ENVOY_LOG(trace, "BSON string: {}", value);
      addString(key, std::move(value));
      break;
    }

    case Field::Type::DOCUMENT: {
      ENVOY_LOG(trace, "BSON document");
      addDocument(key, DocumentImpl::create(data));
      break;
    }

    case Field::Type::ARRAY: {
      ENVOY_LOG(trace, "BSON array");
      addArray(key, DocumentImpl::create(data));
      break;
    }

    case Field::Type::BINARY: {
      std::string value = BufferHelper::removeBinary(data);
      ENVOY_LOG(trace, "BSON binary: {}", value);
      addBinary(key, std::move(value));
      break;
    }

    case Field::Type::OBJECT_ID: {
      Field::ObjectId value;
      BufferHelper::removeBytes(data, &value[0], value.size());
      addObjectId(key, std::move(value));
      break;
    }

    case Field::Type::BOOLEAN: {
      bool value = BufferHelper::removeByte(data) != 0;
      ENVOY_LOG(trace, "BSON boolean: {}", value);
      addBoolean(key, value);
      break;
    }

    case Field::Type::DATETIME: {
      int64_t value = BufferHelper::removeInt64(data);
      ENVOY_LOG(trace, "BSON datetime: {}", value);
      addDatetime(key, value);
      break;
    }

    case Field::Type::NULL_VALUE: {
      ENVOY_LOG(trace, "BSON null value");
      addNull(key);
      break;
    }

    case Field::Type::REGEX: {
      Field::Regex value;
      value.pattern_ = BufferHelper::removeCString(data);
      value.options_ = BufferHelper::removeCString(data);
      ENVOY_LOG(trace, "BSON regex pattern: {} options: {}", value.pattern_, value.options_);
      addRegex(key, std::move(value));
      break;
    }

    case Field::Type::INT32: {
      int32_t value = BufferHelper::removeInt32(data);
      ENVOY_LOG(trace, "BSON int32: {}", value);
      addInt32(key, value);
      break;
    }

    case Field::Type::TIMESTAMP: {
      int64_t value = BufferHelper::removeInt64(data);
      ENVOY_LOG(trace, "BSON timestamp: {}", value);
      addTimestamp(key, value);
      break;
    }

    case Field::Type::INT64: {
      int64_t value = BufferHelper::removeInt64(data);
      ENVOY_LOG(trace, "BSON int64: {}", value);
      addInt64(key, value);
      break;
    }

    default:
      throw EnvoyException(
          fmt::format("invalid BSON element type: {:#x} key: {}", element_type, key));
    }
  }
}

int32_t DocumentImpl::byteSize() const {
  // Minimum size is 5.
  int32_t total_size = sizeof(int32_t) + 1;
  for (const FieldPtr& field : fields_) {
    total_size += field->byteSize();
  }

  return total_size;
}

void DocumentImpl::encode(Buffer::Instance& output) const {
  BufferHelper::writeInt32(output, byteSize());
  for (const FieldPtr& field : fields_) {
    field->encode(output);
  }

  uint8_t done = 0;
  output.add(&done, sizeof(done));
}

bool DocumentImpl::operator==(const Document& rhs) const {
  if (values().size() != rhs.values().size()) {
    return false;
  }

  for (auto i1 = values().begin(), i2 = rhs.values().begin(); i1 != values().end(); i1++, i2++) {
    if (**i1 == **i2) {
      continue;
    }

    return false;
  }

  return true;
}

std::string DocumentImpl::toString() const {
  std::stringstream out;
  out << "{";

  bool first = true;
  for (const FieldPtr& field : fields_) {
    if (!first) {
      out << ", ";
    }

    out << fmt::format("\"{}\": {}", field->key(), field->toString());
    first = false;
  }

  out << "}";
  return out.str();
}

const Field* DocumentImpl::find(const std::string& name) const {
  for (const FieldPtr& field : fields_) {
    if (field->key() == name) {
      return field.get();
    }
  }

  return nullptr;
}

const Field* DocumentImpl::find(const std::string& name, Field::Type type) const {
  for (const FieldPtr& field : fields_) {
    if (field->key() == name && field->type() == type) {
      return field.get();
    }
  }

  return nullptr;
}

} // namespace Bson
} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
