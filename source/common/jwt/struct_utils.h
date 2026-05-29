#pragma once

// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace JwtVerify {

class StructUtils {
public:
  StructUtils(const Protobuf::Struct& struct_pb);

  // NOLINTBEGIN(readability-identifier-naming)
  enum FindResult {
    OK = 0,
    MISSING,
    WRONG_TYPE,
    OUT_OF_RANGE,
  };
  // NOLINTEND(readability-identifier-naming)

  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetString(const std::string& name, std::string* str_value);

  // Return error if the JSON value is not within a positive 64 bit integer
  // range. The decimals in the JSON value are dropped.
  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetUInt64(const std::string& name, uint64_t* int_value);

  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetDouble(const std::string& name, double* double_value);

  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetBoolean(const std::string& name, bool* bool_value);

  // Get string or list of string, designed to get "aud" field
  // "aud" can be either string array or string.
  // Try as string array, read it as empty array if doesn't exist.
  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetStringList(const std::string& name, std::vector<std::string>* list);

  // Find the value with nested names.
  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetValue(const std::string& nested_names, const Protobuf::Value*& found);

private:
  const Protobuf::Struct& struct_pb_;
};

} // namespace JwtVerify
} // namespace Envoy
