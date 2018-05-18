#pragma once

#include <initializer_list>

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"

#include "extensions/filters/network/thrift_proxy/protocol.h"

#include "test/common/buffer/utility.h"

#include "gtest/gtest.h"

using testing::TestParamInfo;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

using Envoy::Buffer::addRepeated;
using Envoy::Buffer::addSeq;
using Envoy::Buffer::addString;
using Envoy::Buffer::bufferToString;

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
