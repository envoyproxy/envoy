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

  // Find the value with nested names: `nested_names` is split on "." and walked as a path into
  // nested JSON objects, so "a.b.c" resolves to `c` inside `b` inside `a`. Returns MISSING if a
  // path element does not exist, and WRONG_TYPE if an intermediate element is not an object.
  //
  // A claim whose name literally contains a "." is therefore not reachable here; use
  // GetLiteralValue. Falling back from one lookup to the other is the caller's choice.
  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetValue(const std::string& nested_names, const Protobuf::Value*& found);

  // Find the value for a single top-level key, matched as an exact whole string. `name` is never
  // split, so this reaches claims whose names contain dots, such as the URL-namespaced
  // "http://example.org/parent_token". Returns MISSING if there is no such top-level key.
  // NOLINTNEXTLINE(readability-identifier-naming)
  FindResult GetLiteralValue(const std::string& name, const Protobuf::Value*& found);

private:
  const Protobuf::Struct& struct_pb_;
};

} // namespace JwtVerify
} // namespace Envoy
