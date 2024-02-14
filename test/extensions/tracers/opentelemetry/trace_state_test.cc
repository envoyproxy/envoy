// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0
//
// Originally taken from the official OpenTelemetry C++ API/SDK
// https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.13.0/api/test/trace/trace_state_test.cc

#include <gtest/gtest.h>

#include "source/extensions/tracers/opentelemetry/trace_state.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Random string of length 257. Used for testing strings with max length 256.
const char* kLongString =
    "4aekid3he76zgytjavudqqeltyvu5zqio2lx7d92dlxlf0z4883irvxuwelsq27sx1mlrjg3r7ad3jeq09rjppyd9veorg"
    "2nmihy4vilabfts8bsxruih0urusmjnglzl3iwpjinmo835dbojcrd73p56nw80v4xxrkye59ytmu5v84ysfa24d58ovv9"
    "w1n54n0mhhf4z0mpv6oudywrp9vfoks6lrvxv3uihvbi2ihazf237kvt1nbsjn3kdvfdb";

// -------------------------- TraceState class tests ---------------------------

std::string createTsReturnHeader(std::string header) {
  auto ts = TraceState::fromHeader(header);
  return ts->toHeader();
}

std::string headerWithMaxMembers() {
  std::string header = "";
  auto max_members = TraceState::kMaxKeyValuePairs;
  for (int i = 0; i < max_members; i++) {
    std::string key = "key" + std::to_string(i);
    std::string value = "value" + std::to_string(i);
    header += key + "=" + value;
    if (i != max_members - 1) {
      header += ",";
    }
  }
  return header;
}

TEST(TraceStateTest, ValidateHeaderParsing) {
  auto max_trace_state_header = headerWithMaxMembers();

  struct {
    const char* input;
    const char* expected;
  } testcases[] = {{"k1=v1", "k1=v1"},
                   {"K1=V1", ""},
                   {"k1=v1,k2=v2,k3=v3", "k1=v1,k2=v2,k3=v3"},
                   {"k1=v1,k2=v2,,", "k1=v1,k2=v2"},
                   {"k1=v1,k2=v2,invalidmember", ""},
                   {"1a-2f@foo=bar1,a*/foo-_/bar=bar4", "1a-2f@foo=bar1,a*/foo-_/bar=bar4"},
                   {"1a-2f@foo=bar1,*/foo-_/bar=bar4", ""},
                   {",k1=v1", "k1=v1"},
                   {",", ""},
                   {",=,", ""},
                   {"", ""},
                   {max_trace_state_header.data(), max_trace_state_header.data()}};
  for (auto& testcase : testcases) {
    EXPECT_EQ(createTsReturnHeader(testcase.input), testcase.expected);
  }
}

TEST(TraceStateTest, TraceStateGet) {
  std::string trace_state_header = headerWithMaxMembers();
  auto ts = TraceState::fromHeader(trace_state_header);

  std::string value;
  EXPECT_TRUE(ts->get("key0", value));
  EXPECT_EQ(value, "value0");
  EXPECT_TRUE(ts->get("key16", value));
  EXPECT_EQ(value, "value16");
  EXPECT_TRUE(ts->get("key31", value));
  EXPECT_EQ(value, "value31");
  EXPECT_FALSE(ts->get("key32", value));
}

TEST(TraceStateTest, TraceStateSet) {
  std::string trace_state_header = "k1=v1,k2=v2";
  auto ts1 = TraceState::fromHeader(trace_state_header);
  auto ts1_new = ts1->set("k3", "v3");
  EXPECT_EQ(ts1_new->toHeader(), "k3=v3,k1=v1,k2=v2");

  trace_state_header = headerWithMaxMembers();
  auto ts2 = TraceState::fromHeader(trace_state_header);

  // adding to max list, should return copy of existing list
  auto ts2_new = ts2->set("n_k1", "n_v1");
  EXPECT_EQ(ts2_new->toHeader(), trace_state_header);

  trace_state_header = "k1=v1,k2=v2";
  auto ts3 = TraceState::fromHeader(trace_state_header);
  auto ts3_new = ts3->set("*n_k1", "n_v1"); // adding invalid key, should return empty
  EXPECT_EQ(ts3_new->toHeader(), "");
}

TEST(TraceStateTest, TraceStateDelete) {
  std::string trace_state_header = "k1=v1,k2=v2,k3=v3";
  auto ts1 = TraceState::fromHeader(trace_state_header);
  auto ts1_new = ts1->remove(std::string("k1"));
  EXPECT_EQ(ts1_new->toHeader(), "k2=v2,k3=v3");

  trace_state_header = "k1=v1"; // single list member
  auto ts2 = TraceState::fromHeader(trace_state_header);
  auto ts2_new = ts2->remove(std::string("k1"));
  EXPECT_EQ(ts2_new->toHeader(), "");

  trace_state_header = "k1=v1"; // single list member, delete invalid entry
  auto ts3 = TraceState::fromHeader(trace_state_header);
  auto ts3_new = ts3->remove(std::string("InvalidKey"));
  EXPECT_EQ(ts3_new->toHeader(), "");
}

TEST(TraceStateTest, Empty) {
  std::string trace_state_header = "";
  auto ts = TraceState::fromHeader(trace_state_header);
  EXPECT_TRUE(ts->empty());

  trace_state_header = "k1=v1,k2=v2";
  auto ts1 = TraceState::fromHeader(trace_state_header);
  EXPECT_FALSE(ts1->empty());
}

TEST(TraceStateTest, GetAllEntries) {
  std::string trace_state_header = "k1=v1,k2=v2,k3=v3";
  auto ts1 = TraceState::fromHeader(trace_state_header);
  const int kNumPairs = 3;
  absl::string_view keys[kNumPairs] = {"k1", "k2", "k3"};
  absl::string_view values[kNumPairs] = {"v1", "v2", "v3"};
  size_t index = 0;
  ts1->getAllEntries([&keys, &values, &index](absl::string_view key, absl::string_view value) {
    EXPECT_EQ(key, keys[index]);
    EXPECT_EQ(value, values[index]);
    index++;
    return true;
  });
}

TEST(TraceStateTest, isValidKey) {
  EXPECT_TRUE(TraceState::isValidKey("valid-key23/*"));
  EXPECT_FALSE(TraceState::isValidKey("Invalid_key"));
  EXPECT_FALSE(TraceState::isValidKey("invalid$Key&"));
  EXPECT_FALSE(TraceState::isValidKey(""));
  EXPECT_FALSE(TraceState::isValidKey(kLongString));
}

TEST(TraceStateTest, isValidValue) {
  EXPECT_TRUE(TraceState::isValidValue("valid-val$%&~"));
  EXPECT_FALSE(TraceState::isValidValue("\t invalid"));
  EXPECT_FALSE(TraceState::isValidValue("invalid="));
  EXPECT_FALSE(TraceState::isValidValue("invalid,val"));
  EXPECT_FALSE(TraceState::isValidValue(""));
  EXPECT_FALSE(TraceState::isValidValue(kLongString));
}

// Tests that keys and values don't depend on null terminators
TEST(TraceStateTest, MemorySafe) {
  std::string trace_state_header = "";
  auto ts = TraceState::fromHeader(trace_state_header);
  const int kNumPairs = 3;
  absl::string_view key_string = "test_key_1test_key_2test_key_3";
  absl::string_view val_string = "test_val_1test_val_2test_val_3";
  absl::string_view keys[kNumPairs] = {key_string.substr(0, 10), key_string.substr(10, 10),
                                       key_string.substr(20, 10)};
  absl::string_view values[kNumPairs] = {val_string.substr(0, 10), val_string.substr(10, 10),
                                         val_string.substr(20, 10)};

  auto ts1 = ts->set(keys[2], values[2]);
  auto ts2 = ts1->set(keys[1], values[1]);
  auto ts3 = ts2->set(keys[0], values[0]);
  size_t index = 0;

  ts3->getAllEntries([&keys, &values, &index](absl::string_view key, absl::string_view value) {
    EXPECT_EQ(key, keys[index]);
    EXPECT_EQ(value, values[index]);
    index++;
    return true;
  });
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
