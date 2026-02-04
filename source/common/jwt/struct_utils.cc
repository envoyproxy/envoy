// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/struct_utils.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace JwtVerify {

StructUtils::StructUtils(const Protobuf::Struct& struct_pb) : struct_pb_(struct_pb) {}

StructUtils::FindResult StructUtils::GetString(const std::string& name, std::string* str_value) {
  const Protobuf::Value* found;
  FindResult result = GetValue(name, found);
  if (result != OK) {
    return result;
  }
  if (found->kind_case() != Protobuf::Value::kStringValue) {
    return WRONG_TYPE;
  }
  *str_value = found->string_value();
  return OK;
}

StructUtils::FindResult StructUtils::GetDouble(const std::string& name, double* double_value) {
  const Protobuf::Value* found;
  FindResult result = GetValue(name, found);
  if (result != OK) {
    return result;
  }
  if (found->kind_case() != Protobuf::Value::kNumberValue) {
    return WRONG_TYPE;
  }
  *double_value = found->number_value();
  return OK;
}

StructUtils::FindResult StructUtils::GetUInt64(const std::string& name, uint64_t* int_value) {
  double double_value;
  FindResult result = GetDouble(name, &double_value);
  if (result != OK) {
    return result;
  }
  if (double_value < 0 ||
      double_value >= static_cast<double>(std::numeric_limits<uint64_t>::max())) {
    return OUT_OF_RANGE;
  }
  *int_value = static_cast<uint64_t>(double_value);
  return OK;
}

StructUtils::FindResult StructUtils::GetBoolean(const std::string& name, bool* bool_value) {
  const Protobuf::Value* found;
  FindResult result = GetValue(name, found);
  if (result != OK) {
    return result;
  }
  if (found->kind_case() != Protobuf::Value::kBoolValue) {
    return WRONG_TYPE;
  }
  *bool_value = found->bool_value();
  return OK;
}

StructUtils::FindResult StructUtils::GetStringList(const std::string& name,
                                                   std::vector<std::string>* list) {
  const Protobuf::Value* found;
  FindResult result = GetValue(name, found);
  if (result != OK) {
    return result;
  }
  if (found->kind_case() == Protobuf::Value::kStringValue) {
    list->push_back(found->string_value());
    return OK;
  }
  if (found->kind_case() == Protobuf::Value::kListValue) {
    for (const auto& v : found->list_value().values()) {
      if (v.kind_case() != Protobuf::Value::kStringValue) {
        return WRONG_TYPE;
      }
      list->push_back(v.string_value());
    }
    return OK;
  }
  return WRONG_TYPE;
}

StructUtils::FindResult StructUtils::GetValue(const std::string& nested_names,
                                              const Protobuf::Value*& found) {
  const std::vector<absl::string_view> name_vector = absl::StrSplit(nested_names, '.');

  const Protobuf::Struct* current_struct = &struct_pb_;
  for (size_t i = 0; i < name_vector.size(); ++i) {
    const auto& fields = current_struct->fields();
    const auto it = fields.find(std::string(name_vector[i]));
    if (it == fields.end()) {
      return MISSING;
    }
    if (i == name_vector.size() - 1) {
      found = &it->second;
      return OK;
    }
    if (it->second.kind_case() != Protobuf::Value::kStructValue) {
      return WRONG_TYPE;
    }
    current_struct = &it->second.struct_value();
  }
  return MISSING;
}

} // namespace JwtVerify
} // namespace Envoy
