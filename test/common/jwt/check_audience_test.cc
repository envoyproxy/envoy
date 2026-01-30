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
// limitations under the License.

#include "source/common/jwt/check_audience.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

TEST(CheckAudienceTest, TestConfigNotPrefixNotTailing) {
  CheckAudience checker({"example_service"});
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service/"}));
}

TEST(CheckAudienceTest, TestConfigHttpPrefixNotTailing) {
  CheckAudience checker({"http://example_service"});
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service/"}));
}

TEST(CheckAudienceTest, TestConfigHttpsPrefixNotTailing) {
  CheckAudience checker({"https://example_service"});
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service/"}));
}

TEST(CheckAudienceTest, TestConfigNotPrefixWithTailing) {
  CheckAudience checker({"example_service/"});
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service/"}));
}

TEST(CheckAudienceTest, TestConfigHttpPrefixWithTailing) {
  CheckAudience checker({"http://example_service/"});
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service/"}));
}

TEST(CheckAudienceTest, TestConfigHttpsPrefixWithTailing) {
  CheckAudience checker({"https://example_service/"});
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"https://example_service"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"example_service/"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"http://example_service/"}));
}

TEST(CheckAudienceTest, TestAudiencesAllowedWhenNoAudiencesConfigured) {
  CheckAudience checker({});
  EXPECT_TRUE(checker.areAudiencesAllowed({"foo", "bar"}));
}

TEST(CheckAudienceTest, TestAnyAudienceMatch) {
  CheckAudience checker({"bar", "quux", "foo"});
  EXPECT_TRUE(checker.areAudiencesAllowed({"quux"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({"baz", "quux"}));
}

TEST(CheckAudienceTest, TestEmptyAudienceMatch) {
  CheckAudience checker({"bar", ""});
  EXPECT_TRUE(checker.areAudiencesAllowed({"bar"}));
  EXPECT_TRUE(checker.areAudiencesAllowed({""}));
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
