#include <string>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/dynamic_modules/json_pointer.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {
namespace {

using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOkAndHolds;

nlohmann::json chatPayload() {
  return nlohmann::json::parse(R"({
    "model": "gpt-4o",
    "messages": [{"role": "user", "content": "hi"}, {"role": "assistant", "content": "hello"}],
    "a/b": 1,
    "m~n": 2,
    "": 3
  })");
}

nlohmann::json offloadedPayload() {
  nlohmann::json document = chatPayload();
  document["messages"][0]["content"] =
      JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{40, 2048});
  return document;
}

TEST(JsonPointerGetTest, ReadsValues) {
  const nlohmann::json document = chatPayload();
  EXPECT_THAT(getJson(document, "/model"), IsOkAndHolds("\"gpt-4o\""));
  EXPECT_THAT(getJson(document, "/messages/1/role"), IsOkAndHolds("\"assistant\""));
  EXPECT_THAT(getJson(document, "/messages/0"), IsOkAndHolds(R"({"content":"hi","role":"user"})"));
  EXPECT_THAT(getJson(document, ""), IsOkAndHolds(document.dump()));
}

TEST(JsonPointerGetTest, UnescapesTokens) {
  const nlohmann::json document = chatPayload();
  EXPECT_THAT(getJson(document, "/a~1b"), IsOkAndHolds("1"));
  EXPECT_THAT(getJson(document, "/m~0n"), IsOkAndHolds("2"));
  EXPECT_THAT(getJson(document, "/"), IsOkAndHolds("3"));
}

TEST(JsonPointerGetTest, RejectsMalformedPointers) {
  const nlohmann::json document = chatPayload();
  EXPECT_THAT(getJson(document, "model"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(getJson(document, "/m~2n"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(getJson(document, "/m~"), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST(JsonPointerGetTest, ReportsMissingValues) {
  const nlohmann::json document = chatPayload();
  for (const absl::string_view pointer : {"/missing", "/messages/2", "/messages/01", "/messages/-",
                                          "/messages/+1", "/model/0", "/messages/0/role/x"}) {
    EXPECT_THAT(getJson(document, pointer), HasStatusCode(absl::StatusCode::kNotFound)) << pointer;
  }
}

TEST(JsonPointerGetTest, ReportsOffloadedValues) {
  const nlohmann::json document = offloadedPayload();
  EXPECT_THAT(getJson(document, "/messages/0/content"),
              HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(getJson(document, "/messages"), HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(getJson(document, ""), HasStatusCode(absl::StatusCode::kFailedPrecondition));
  // The parts held in memory stay readable.
  EXPECT_THAT(getJson(document, "/messages/0/role"), IsOkAndHolds("\"user\""));
  EXPECT_THAT(getJson(document, "/messages/1"),
              IsOkAndHolds(R"({"content":"hello","role":"assistant"})"));
}

TEST(JsonPointerGetTest, ReplacesInvalidUtf8) {
  nlohmann::json document;
  document["bad"] = std::string("\xff", 1);
  EXPECT_THAT(getJson(document, "/bad"), IsOkAndHolds("\"\xEF\xBF\xBD\""));
}

TEST(JsonPointerSetTest, ReplacesAndAddsMembers) {
  nlohmann::json document = chatPayload();
  ASSERT_OK(setJson(document, "/model", "\"claude\""));
  ASSERT_OK(setJson(document, "/metadata", R"({"tenant":"acme"})"));
  ASSERT_OK(setJson(document, "/metadata/tier", "\"gold\""));
  EXPECT_EQ(document["model"], "claude");
  EXPECT_EQ(document["metadata"], nlohmann::json::parse(R"({"tenant":"acme","tier":"gold"})"));
}

TEST(JsonPointerSetTest, ReplacesAndAppendsArrayElements) {
  nlohmann::json document = chatPayload();
  ASSERT_OK(setJson(document, "/messages/1/content", "\"bye\""));
  ASSERT_OK(setJson(document, "/messages/-", R"({"role":"user","content":"again"})"));
  ASSERT_EQ(document["messages"].size(), 3);
  EXPECT_EQ(document["messages"][1]["content"], "bye");
  EXPECT_EQ(document["messages"][2]["content"], "again");
}

TEST(JsonPointerSetTest, ReplacesOffloadedValues) {
  nlohmann::json document = offloadedPayload();
  ASSERT_OK(setJson(document, "/messages/0/content", "\"redacted\""));
  EXPECT_THAT(getJson(document, "/messages/0/content"), IsOkAndHolds("\"redacted\""));
}

TEST(JsonPointerSetTest, ReplacesTheRootWithAnObject) {
  nlohmann::json document = chatPayload();
  ASSERT_OK(setJson(document, "", R"({"model":"other"})"));
  EXPECT_EQ(document, nlohmann::json::parse(R"({"model":"other"})"));
  EXPECT_THAT(setJson(document, "", "[]"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(document, nlohmann::json::parse(R"({"model":"other"})"));
}

TEST(JsonPointerSetTest, RejectsInvalidInput) {
  nlohmann::json document = chatPayload();
  EXPECT_THAT(setJson(document, "model", "1"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(setJson(document, "/model", "{"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(setJson(document, "/model", ""), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(document, chatPayload());
}

TEST(JsonPointerSetTest, ReportsMissingParents) {
  nlohmann::json document = chatPayload();
  for (const absl::string_view pointer :
       {"/missing/key", "/messages/5", "/messages/2", "/model/key", "/messages/x"}) {
    EXPECT_THAT(setJson(document, pointer, "1"), HasStatusCode(absl::StatusCode::kNotFound))
        << pointer;
  }
  EXPECT_EQ(document, chatPayload());
}

TEST(JsonPointerRemoveTest, RemovesMembersAndElements) {
  nlohmann::json document = chatPayload();
  ASSERT_OK(removeJson(document, "/a~1b"));
  ASSERT_OK(removeJson(document, "/messages/0"));
  EXPECT_FALSE(document.contains("a/b"));
  ASSERT_EQ(document["messages"].size(), 1);
  EXPECT_EQ(document["messages"][0]["role"], "assistant");
}

TEST(JsonPointerRemoveTest, RejectsInvalidInput) {
  nlohmann::json document = chatPayload();
  EXPECT_THAT(removeJson(document, ""), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(removeJson(document, "model"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(document, chatPayload());
}

TEST(JsonPointerRemoveTest, ReportsMissingValues) {
  nlohmann::json document = chatPayload();
  for (const absl::string_view pointer :
       {"/missing", "/messages/2", "/messages/-", "/model/0", "/missing/key"}) {
    EXPECT_THAT(removeJson(document, pointer), HasStatusCode(absl::StatusCode::kNotFound))
        << pointer;
  }
  EXPECT_EQ(document, chatPayload());
}

} // namespace
} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
