#pragma once

#include <initializer_list>

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"

#include "extensions/filters/network/thrift_proxy/protocol.h"

#include "gtest/gtest.h"

using testing::TestParamInfo;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

inline void addInt16(Buffer::Instance& buffer, int16_t value) {
  value = htobe16(value);
  buffer.add(&value, 2);
}

inline void addInt32(Buffer::Instance& buffer, int32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

inline void addInt8(Buffer::Instance& buffer, int8_t value) { buffer.add(&value, 1); }

inline void addInt8(Buffer::Instance& buffer, MessageType value) { buffer.add(&value, 1); }

inline void addInt8(Buffer::Instance& buffer, FieldType value) { buffer.add(&value, 1); }

inline void addRepeated(Buffer::Instance& buffer, int n, int8_t value) {
  for (int i = 0; i < n; i++) {
    buffer.add(&value, 1);
  }
}

inline void addSeq(Buffer::Instance& buffer, const std::initializer_list<uint8_t> values) {
  for (int8_t value : values) {
    buffer.add(&value, 1);
  }
}

inline void addString(Buffer::Instance& buffer, const std::string& s) { buffer.add(s); }

inline std::string bufferToString(Buffer::Instance& buffer) {
  if (buffer.length() == 0) {
    return "";
  }

  char* data = static_cast<char*>(buffer.linearize(buffer.length()));
  return std::string(data, buffer.length());
}

inline std::string fieldTypeToString(const FieldType& field_type) {
  switch (field_type) {
  case FieldType::Stop:
    return "Stop";
  case FieldType::Void:
    return "Void";
  case FieldType::Bool:
    return "Bool";
  case FieldType::Byte:
    return "Byte";
  case FieldType::Double:
    return "Double";
  case FieldType::I16:
    return "I16";
  case FieldType::I32:
    return "I32";
  case FieldType::I64:
    return "I64";
  case FieldType::String:
    return "String";
  case FieldType::Struct:
    return "Struct";
  case FieldType::Map:
    return "Map";
  case FieldType::Set:
    return "Set";
  case FieldType::List:
    return "List";
  default:
    return "UnknownFieldType";
  }
}

inline std::string fieldTypeParamToString(const TestParamInfo<FieldType>& params) {
  return fieldTypeToString(params.param);
}

} // namespace
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
