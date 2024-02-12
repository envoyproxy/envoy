// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0
//
// Originally taken from the official OpenTelemetry C++ API/SDK
// https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.13.0/api/test/common/kv_properties_test.cc

#include <string>
#include <utility>
#include <vector>

#include "source/extensions/tracers/opentelemetry/kv_properties.h"

#include "gtest/gtest.h"

// ------------------------- Entry class tests ---------------------------------
namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test constructor that takes a key-value pair
TEST(EntryTest, KeyValueConstruction) {
  absl::string_view key = "test_key";
  absl::string_view val = "test_value";
  KeyValueProperties::Entry e(key, val);

  EXPECT_EQ(key.size(), e.getKey().size());
  EXPECT_EQ(key, e.getKey());

  EXPECT_EQ(val.size(), e.getValue().size());
  EXPECT_EQ(val, e.getValue());
}

// Test copy constructor
TEST(EntryTest, Copy) {
  KeyValueProperties::Entry e("test_key", "test_value");
  KeyValueProperties::Entry copy(e);
  EXPECT_EQ(copy.getKey(), e.getKey());
  EXPECT_EQ(copy.getValue(), e.getValue());

  e.setValue("changed_value");
  EXPECT_NE(copy.getValue(), e.getValue());
}

// Test assignment operator
TEST(EntryTest, Assignment) {
  KeyValueProperties::Entry e("test_key", "test_value");
  KeyValueProperties::Entry empty;
  empty = e;
  EXPECT_EQ(empty.getKey(), e.getKey());
  EXPECT_EQ(empty.getValue(), e.getValue());
}

TEST(EntryTest, SetValue) {
  KeyValueProperties::Entry e("test_key", "test_value");
  absl::string_view new_val = "new_value";
  e.setValue(new_val);

  EXPECT_EQ(new_val.size(), e.getValue().size());
  EXPECT_EQ(new_val, e.getValue());
}

// // ------------------------- KeyValueStringTokenizer tests ---------------------------------

TEST(KVStringTokenizer, SinglePair) {
  bool valid_kv;
  absl::string_view key, value;
  absl::string_view str = "k1=v1";
  KeyValueStringTokenizerOptions opts;
  KeyValueStringTokenizer tk(str, opts);
  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_TRUE(valid_kv);
  EXPECT_EQ(key, "k1");
  EXPECT_EQ(value, "v1");
  EXPECT_FALSE(tk.next(valid_kv, key, value));
}

TEST(KVStringTokenizer, AcceptEmptyEntries) {
  bool valid_kv;
  absl::string_view key, value;
  absl::string_view str = ":k1=v1::k2=v2: ";
  KeyValueStringTokenizerOptions opts;
  opts.member_separator = ':';
  opts.ignore_empty_members = false;

  KeyValueStringTokenizer tk(str, opts);
  EXPECT_TRUE(tk.next(valid_kv, key, value)); // empty pair
  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_TRUE(valid_kv);
  EXPECT_EQ(key, "k1");
  EXPECT_EQ(value, "v1");
  EXPECT_TRUE(tk.next(valid_kv, key, value)); // empty pair
  EXPECT_EQ(key, "");
  EXPECT_EQ(value, "");
  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_EQ(key, "k2");
  EXPECT_EQ(value, "v2");
  EXPECT_TRUE(tk.next(valid_kv, key, value)); // empty pair
  EXPECT_EQ(key, "");
  EXPECT_EQ(value, "");
  EXPECT_FALSE(tk.next(valid_kv, key, value));
}

TEST(KVStringTokenizer, ValidPairsWithEmptyEntries) {
  absl::string_view str = "k1:v1===k2:v2==";
  bool valid_kv;
  absl::string_view key, value;
  KeyValueStringTokenizerOptions opts;
  opts.member_separator = '=';
  opts.key_value_separator = ':';

  KeyValueStringTokenizer tk(str, opts);
  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_TRUE(valid_kv);
  EXPECT_EQ(key, "k1");
  EXPECT_EQ(value, "v1");

  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_TRUE(valid_kv);
  EXPECT_EQ(key, "k2");
  EXPECT_EQ(value, "v2");

  EXPECT_FALSE(tk.next(valid_kv, key, value));
}

TEST(KVStringTokenizer, InvalidPairs) {
  absl::string_view str = "k1=v1,invalid  ,,  k2=v2   ,invalid";
  KeyValueStringTokenizer tk(str);
  bool valid_kv;
  absl::string_view key, value;
  EXPECT_TRUE(tk.next(valid_kv, key, value));

  EXPECT_TRUE(valid_kv);
  EXPECT_EQ(key, "k1");
  EXPECT_EQ(value, "v1");

  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_FALSE(valid_kv);

  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_TRUE(valid_kv);
  EXPECT_EQ(key, "k2");
  EXPECT_EQ(value, "v2");

  EXPECT_TRUE(tk.next(valid_kv, key, value));
  EXPECT_FALSE(valid_kv);

  EXPECT_FALSE(tk.next(valid_kv, key, value));
}

TEST(KVStringTokenizer, NumTokens) {
  struct {
    const char* input;
    size_t expected;
  } testcases[] = {{"k1=v1", 1},
                   {" ", 1},
                   {"k1=v1,k2=v2,k3=v3", 3},
                   {"k1=v1,", 1},
                   {"k1=v1,k2=v2,invalidmember", 3},
                   {"", 0}};
  for (auto& testcase : testcases) {
    KeyValueStringTokenizer tk(testcase.input);
    EXPECT_EQ(tk.numTokens(), testcase.expected);
  }
}

// //------------------------- KeyValueProperties tests ---------------------------------

TEST(KeyValueProperties, addEntry) {
  auto kv_properties = KeyValueProperties(1);
  kv_properties.addEntry("k1", "v1");
  std::string value;
  bool present = kv_properties.getValue("k1", value);
  EXPECT_TRUE(present);
  EXPECT_EQ(value, "v1");

  kv_properties.addEntry("k2", "v2"); // entry will not be added as max size reached.
  EXPECT_EQ(kv_properties.size(), 1);
  present = kv_properties.getValue("k2", value);
  EXPECT_FALSE(present);
}

TEST(KeyValueProperties, getValue) {
  auto kv_properties = KeyValueProperties(1);
  kv_properties.addEntry("k1", "v1");
  std::string value;
  bool present = kv_properties.getValue("k1", value);
  EXPECT_TRUE(present);
  EXPECT_EQ(value, "v1");

  present = kv_properties.getValue("k3", value);
  EXPECT_FALSE(present);
}

TEST(KeyValueProperties, GetAllEntries) {
  const size_t kNumPairs = 3;
  absl::string_view keys[kNumPairs] = {"k1", "k2", "k3"};
  absl::string_view values[kNumPairs] = {"v1", "v2", "v3"};

  auto kv_properties = KeyValueProperties(kNumPairs);
  kv_properties.addEntry("k1", "v1");
  kv_properties.addEntry("k2", "v2");
  kv_properties.addEntry("k3", "v3");

  size_t index = 0;
  kv_properties.getAllEntries(
      [&keys, &values, &index](absl::string_view key, absl::string_view value) {
        EXPECT_EQ(key, keys[index]);
        EXPECT_EQ(value, values[index]);
        index++;
        return true;
      });

  EXPECT_EQ(index, kNumPairs);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
