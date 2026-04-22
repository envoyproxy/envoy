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

  enum FindResult {
    OK = 0,
    MISSING,
    WRONG_TYPE,
    OUT_OF_RANGE,
  };

  FindResult GetString(const std::string& name, std::string* str_value);

  // Return error if the JSON value is not within a positive 64 bit integer
  // range. The decimals in the JSON value are dropped.
  FindResult GetUInt64(const std::string& name, uint64_t* int_value);

  FindResult GetDouble(const std::string& name, double* double_value);

  FindResult GetBoolean(const std::string& name, bool* bool_value);

  // Get string or list of string, designed to get "aud" field
  // "aud" can be either string array or string.
  // Try as string array, read it as empty array if doesn't exist.
  FindResult GetStringList(const std::string& name, std::vector<std::string>* list);

  // Find the value with nested names.
  FindResult GetValue(const std::string& nested_names, const Protobuf::Value*& found);

private:
  const Protobuf::Struct& struct_pb_;
};

} // namespace JwtVerify
} // namespace Envoy
