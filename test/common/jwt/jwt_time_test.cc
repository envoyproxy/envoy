// Copyright 2020 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/jwt.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// SPELLCHECKER(off)
// Header:  {"alg":"RS256","typ":"JWT"}
// Payload: {
//    "iss":"https://example.com",
//    "sub":"test@example.com",
//    "exp": 1605052800,
//    "nbf": 1605050800
// }
// SPELLCHECKER(on)
const std::string JwtText =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "ewogICJpc3MiOiAiaHR0cHM6Ly9leGFtcGxlLmNvbSIsCiAgInN1YiI6ICJ0ZXN0QGV4YW1wbG"
    "UuY29tIiwKICAiZXhwIjogMTYwNTA1MjgwMCwKICAibmJmIjogMTYwNTA1MDgwMAp9."
    "digk0Fr_IdcWgJNVyeVDw2dC1cQG6LsHwg5pIN93L4";

// The exp time for above Jwt
constexpr uint64_t ExpTime = 1605052800U;

// The nbf time for above Jwt.
constexpr uint64_t NbfTime = 1605050800U;

TEST(VerifyExpTest, BothNbfExp) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtText), Status::Ok);

  // 10s before exp
  EXPECT_EQ(jwt.verifyTimeConstraint(ExpTime + kClockSkewInSecond - 10), Status::Ok);
  // 10s after exp
  EXPECT_EQ(jwt.verifyTimeConstraint(ExpTime + kClockSkewInSecond + 10), Status::JwtExpired);

  // 10s after nbf
  EXPECT_EQ(jwt.verifyTimeConstraint(NbfTime - kClockSkewInSecond + 10), Status::Ok);
  // 10s before nbf
  EXPECT_EQ(jwt.verifyTimeConstraint(NbfTime - kClockSkewInSecond - 10), Status::JwtNotYetValid);
}

TEST(VerifyExpTest, BothNbfExpWithCustomClockSkew) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtText), Status::Ok);

  constexpr uint64_t kCustomClockSkew = 10;
  // 10s before exp
  EXPECT_EQ(jwt.verifyTimeConstraint(ExpTime + kCustomClockSkew - 1, kCustomClockSkew), Status::Ok);
  // 10s after exp
  EXPECT_EQ(jwt.verifyTimeConstraint(ExpTime + kCustomClockSkew + 1, kCustomClockSkew),
            Status::JwtExpired);

  // 10s after nbf
  EXPECT_EQ(jwt.verifyTimeConstraint(NbfTime - kCustomClockSkew + 1, kCustomClockSkew), Status::Ok);
  // 10s before nbf
  EXPECT_EQ(jwt.verifyTimeConstraint(NbfTime - kCustomClockSkew - 1, kCustomClockSkew),
            Status::JwtNotYetValid);
}

TEST(VerifyExpTest, OnlyExp) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtText), Status::Ok);
  // Reset nbf
  jwt.nbf_ = 0;

  // 10s before exp
  EXPECT_EQ(jwt.verifyTimeConstraint(ExpTime + kClockSkewInSecond - 10), Status::Ok);
  // 10s after exp
  EXPECT_EQ(jwt.verifyTimeConstraint(ExpTime + kClockSkewInSecond + 10), Status::JwtExpired);

  // `Now` can be 0,
  EXPECT_EQ(jwt.verifyTimeConstraint(0), Status::Ok);
}

TEST(VerifyExpTest, OnlyNbf) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtText), Status::Ok);
  // Reset exp
  jwt.exp_ = 0;

  // `Now` can be very large
  EXPECT_EQ(jwt.verifyTimeConstraint(9223372036854775810U), Status::Ok);

  // 10s after nbf
  EXPECT_EQ(jwt.verifyTimeConstraint(NbfTime - kClockSkewInSecond + 10), Status::Ok);
  // 10s before nbf
  EXPECT_EQ(jwt.verifyTimeConstraint(NbfTime - kClockSkewInSecond - 10), Status::JwtNotYetValid);
}

TEST(VerifyExpTest, NotTimeConstraint) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtText), Status::Ok);
  // Reset both exp and nbf
  jwt.exp_ = 0;
  jwt.nbf_ = 0;

  // `Now` can be very large
  EXPECT_EQ(jwt.verifyTimeConstraint(9223372036854775810U), Status::Ok);

  // `Now` can be 0,
  EXPECT_EQ(jwt.verifyTimeConstraint(0), Status::Ok);
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
