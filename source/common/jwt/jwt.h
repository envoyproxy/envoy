// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.#pragma once

#pragma once

#include <string>
#include <vector>

#include "google/protobuf/struct.pb.h"
#include "source/common/jwt/status.h"

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
  ::google::protobuf::Struct header_pb_;

  // payload string
  std::string payload_str_;
  // payload base64_url encoded
  std::string payload_str_base64url_;
  // payload in Struct protobuf
  ::google::protobuf::Struct payload_pb_;
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
