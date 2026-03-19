#pragma once

// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "source/common/jwt/status.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace JwtVerify {

// Clock skew defaults to one minute.
constexpr uint64_t kClockSkewInSecond = 60;

/**
 * struct to hold a JWT data.
 */
struct Jwt {
  // entire jwt
  std::string jwt_;

  // header string
  std::string header_str_;
  // header base64_url encoded
  std::string header_str_base64url_;
  // header in Struct protobuf
  Protobuf::Struct header_pb_;

  // payload string
  std::string payload_str_;
  // payload base64_url encoded
  std::string payload_str_base64url_;
  // payload in Struct protobuf
  Protobuf::Struct payload_pb_;
  // signature string
  std::string signature_;
  // alg
  std::string alg_;
  // kid
  std::string kid_;
  // iss
  std::string iss_;
  // audiences
  std::vector<std::string> audiences_;
  // sub
  std::string sub_;
  // issued at
  uint64_t iat_ = 0;
  // not before
  uint64_t nbf_ = 0;
  // expiration
  uint64_t exp_ = 0;
  // JWT ID
  std::string jti_;

  /**
   * Standard constructor.
   */
  Jwt() {}
  /**
   * Copy constructor. The copy constructor is marked as explicit as the caller
   * should understand the copy operation is non-trivial as a complete
   * re-deserialization occurs.
   * @param rhs the instance to copy.
   */
  explicit Jwt(const Jwt& instance);

  /**
   * Copy Jwt instance.
   * @param rhs the instance to copy.
   * @return this
   */
  Jwt& operator=(const Jwt& rhs);

  /**
   * Parse Jwt from string text
   * @return the status.
   */
  Status parseFromString(const std::string& jwt);

  /*
   * Verify Jwt time constraint if specified
   * esp: expiration time, nbf: not before time.
   * @param now: is the current time in seconds since the unix epoch
   * @param clock_skew: the the clock skew in second.
   * @return the verification status.
   */
  Status verifyTimeConstraint(uint64_t now, uint64_t clock_skew = kClockSkewInSecond) const;
};

} // namespace JwtVerify
} // namespace Envoy
