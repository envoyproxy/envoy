#include <string>

#include "source/common/json/wuffs_json/ai_request_handlers.h"
#include "source/common/json/wuffs_json/wuffs_json_cursor.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace Wuffs {
namespace {

// Feed a complete JSON string in one shot.
absl::Status feedOne(absl::string_view json, WuffsJsonCursor::Handler& h) {
  WuffsJsonCursor cursor(h);
  return cursor.feed(json, /*closed=*/true);
}

// Feed a JSON string in fixed-size chunks to exercise boundary handling.
absl::Status feedChunked(absl::string_view json, WuffsJsonCursor::Handler& h,
                         size_t chunk_size = 4) {
  WuffsJsonCursor cursor(h);
  while (!json.empty()) {
    size_t n = std::min(chunk_size, json.size());
    bool closed = (n == json.size());
    if (auto s = cursor.feed(json.substr(0, n), closed); !s.ok()) {
      return s;
    }
    json.remove_prefix(n);
  }
  return absl::OkStatus();
}

// ============================================================================
// InferenceBodyHandler
// ============================================================================

TEST(InferenceBodyHandlerTest, BasicModelAndStream) {
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"model":"gpt-4o","stream":true,"messages":[]})", h).ok());
  EXPECT_TRUE(h.isValid());
  EXPECT_EQ(h.model(), "gpt-4o");
  EXPECT_TRUE(h.hasModel());
  EXPECT_TRUE(h.stream());
  EXPECT_TRUE(h.hasStream());
}

TEST(InferenceBodyHandlerTest, StreamDefaultsFalse) {
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"model":"claude-sonnet-4-6","messages":[]})", h).ok());
  EXPECT_FALSE(h.stream());
  EXPECT_FALSE(h.hasStream());
}

TEST(InferenceBodyHandlerTest, MissingModelIsInvalid) {
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"stream":false,"messages":[]})", h).ok());
  EXPECT_FALSE(h.isValid());
  EXPECT_FALSE(h.hasModel());
  EXPECT_EQ(h.model(), "");
}

TEST(InferenceBodyHandlerTest, SamplingScalars) {
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(
      R"({"model":"gpt-4","temperature":0.7,"max_tokens":1024,"top_p":0.9,"n":2,"seed":42})", h)
                  .ok());
  EXPECT_DOUBLE_EQ(*h.temperature(), 0.7);
  EXPECT_EQ(*h.maxTokens(), 1024);
  EXPECT_DOUBLE_EQ(*h.topP(), 0.9);
  EXPECT_EQ(*h.numCompletions(), 2);
  EXPECT_EQ(*h.seed(), 42);
}

TEST(InferenceBodyHandlerTest, AbsentSamplingScalarsAreNullopt) {
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"model":"gpt-4"})", h).ok());
  EXPECT_FALSE(h.temperature().has_value());
  EXPECT_FALSE(h.maxTokens().has_value());
  EXPECT_FALSE(h.topP().has_value());
  EXPECT_FALSE(h.numCompletions().has_value());
  EXPECT_FALSE(h.seed().has_value());
}

TEST(InferenceBodyHandlerTest, MessagesElementByteRanges) {
  const std::string body =
      R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"hello"}]})";
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(body, h).ok());

  ASSERT_EQ(h.messages().size(), 2u);
  for (const auto& r : h.messages()) {
    EXPECT_LT(r.start, r.end);
    EXPECT_LE(r.end, body.size());
    EXPECT_EQ(body[r.start], '{');
    EXPECT_EQ(body[r.end - 1], '}');
  }
}

TEST(InferenceBodyHandlerTest, ToolsElementByteRanges) {
  const std::string body =
      R"({"model":"gpt-4","tools":[{"type":"function","function":{"name":"get_weather"}}]})";
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(body, h).ok());

  ASSERT_EQ(h.tools().size(), 1u);
  EXPECT_EQ(body[h.tools()[0].start], '{');
  EXPECT_EQ(body[h.tools()[0].end - 1], '}');
}

TEST(InferenceBodyHandlerTest, EmptyMessagesArray) {
  InferenceBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"model":"gpt-4","messages":[]})", h).ok());
  EXPECT_TRUE(h.messages().empty());
}

TEST(InferenceBodyHandlerTest, DuplicateModelKeyAborts) {
  InferenceBodyHandler h;
  auto s = feedOne(R"({"model":"gpt-4","model":"gpt-3.5"})", h);
  EXPECT_FALSE(s.ok());
  EXPECT_TRUE(h.hasDuplicateKeys());
}

TEST(InferenceBodyHandlerTest, DuplicateStreamKeyAborts) {
  InferenceBodyHandler h;
  auto s = feedOne(R"({"model":"gpt-4","stream":true,"stream":false})", h);
  EXPECT_FALSE(s.ok());
  EXPECT_TRUE(h.hasDuplicateKeys());
}

TEST(InferenceBodyHandlerTest, UnknownKeysIgnored) {
  InferenceBodyHandler h;
  ASSERT_TRUE(
      feedOne(R"({"model":"gpt-4","unknown_field":"value","another":42,"messages":[]})", h).ok());
  EXPECT_EQ(h.model(), "gpt-4");
  EXPECT_TRUE(h.messages().empty());
}

TEST(InferenceBodyHandlerTest, ChunkedFeeding) {
  InferenceBodyHandler h;
  const std::string body =
      R"({"model":"claude-sonnet-4-6","stream":false,"temperature":1.0,"messages":[{"role":"user","content":"hello"}]})";
  ASSERT_TRUE(feedChunked(body, h, /*chunk_size=*/5).ok());
  EXPECT_EQ(h.model(), "claude-sonnet-4-6");
  EXPECT_FALSE(h.stream());
  EXPECT_DOUBLE_EQ(*h.temperature(), 1.0);
  ASSERT_EQ(h.messages().size(), 1u);
}

TEST(InferenceBodyHandlerTest, MultipleMessagesChunked) {
  InferenceBodyHandler h;
  const std::string body =
      R"({"model":"gpt-4","messages":[{"role":"system","content":"You are helpful."},{"role":"user","content":"Hello!"},{"role":"assistant","content":"Hi there!"}]})";
  ASSERT_TRUE(feedChunked(body, h, /*chunk_size=*/7).ok());
  EXPECT_EQ(h.messages().size(), 3u);
}

// ============================================================================
// AgentBodyHandler
// ============================================================================

TEST(AgentBodyHandlerTest, BasicToolsCall) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"get_weather","arguments":{"city":"NY"}}})",
      h)
                  .ok());
  EXPECT_EQ(h.jsonrpc(), "2.0");
  EXPECT_EQ(h.method(), "tools/call");
  EXPECT_EQ(h.id(), "1");
  EXPECT_EQ(h.paramsName(), "get_weather");
  EXPECT_TRUE(h.isValidJsonRpc());
  EXPECT_FALSE(h.isResponse());
}

TEST(AgentBodyHandlerTest, IdAsString) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"jsonrpc":"2.0","method":"ping","id":"req-abc-123"})", h).ok());
  EXPECT_EQ(h.id(), "req-abc-123");
}

TEST(AgentBodyHandlerTest, IdAsNull) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"jsonrpc":"2.0","method":"notifications/message","id":null})", h).ok());
  EXPECT_EQ(h.id(), "");
}

TEST(AgentBodyHandlerTest, ResponseWithResult) {
  AgentBodyHandler h;
  ASSERT_TRUE(
      feedOne(R"({"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"hi"}]}})", h)
          .ok());
  EXPECT_TRUE(h.isResponse());
  EXPECT_FALSE(h.hasDuplicateKeys());
}

TEST(AgentBodyHandlerTest, ResponseWithError) {
  AgentBodyHandler h;
  ASSERT_TRUE(
      feedOne(R"({"jsonrpc":"2.0","id":1,"error":{"code":-32600,"message":"Invalid Request"}})", h)
          .ok());
  EXPECT_TRUE(h.isResponse());
}

TEST(AgentBodyHandlerTest, InvalidJsonRpcVersion) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"jsonrpc":"1.0","method":"ping","id":1})", h).ok());
  EXPECT_FALSE(h.isValidJsonRpc());
}

TEST(AgentBodyHandlerTest, ParamsUri) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(
      R"({"jsonrpc":"2.0","method":"resources/read","id":2,"params":{"uri":"file:///tmp/x.txt"}})",
      h)
                  .ok());
  EXPECT_EQ(h.paramsUri(), "file:///tmp/x.txt");
}

TEST(AgentBodyHandlerTest, ParamsProtocolVersionAndClientInfo) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"my-client"}}})",
      h)
                  .ok());
  EXPECT_EQ(h.paramsProtocolVersion(), "2025-03-26");
  EXPECT_EQ(h.clientInfoName(), "my-client");
}

TEST(AgentBodyHandlerTest, ParamsLevelAndRequestIdAsString) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(
      R"({"jsonrpc":"2.0","method":"logging/setLevel","id":3,"params":{"level":"debug","requestId":"rid-99"}})",
      h)
                  .ok());
  EXPECT_EQ(h.paramsLevel(), "debug");
  EXPECT_EQ(h.paramsRequestId(), "rid-99");
}

TEST(AgentBodyHandlerTest, ParamsRequestIdAsNumber) {
  AgentBodyHandler h;
  ASSERT_TRUE(
      feedOne(R"({"jsonrpc":"2.0","method":"ping","id":5,"params":{"requestId":42}})", h).ok());
  EXPECT_EQ(h.paramsRequestId(), "42");
}

TEST(AgentBodyHandlerTest, MetaTraceContext) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"fn","_meta":{"traceparent":"00-abc-def-01","tracestate":"vendor=value","baggage":"k=v"}}})",
      h)
                  .ok());
  EXPECT_EQ(h.metaTraceparent(), "00-abc-def-01");
  EXPECT_EQ(h.metaTracestate(), "vendor=value");
  EXPECT_EQ(h.metaBaggage(), "k=v");
}

TEST(AgentBodyHandlerTest, ParamsArgumentsDictByteRange) {
  const std::string body =
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"search","arguments":{"query":"envoy","limit":10}}})";
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(body, h).ok());

  ASSERT_TRUE(h.paramsArguments().has_value());
  const auto& r = *h.paramsArguments();
  EXPECT_EQ(body.substr(r.start, r.end - r.start), R"({"query":"envoy","limit":10})");
}

TEST(AgentBodyHandlerTest, ParamsArgumentsArrayByteRange) {
  const std::string body =
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"fn","arguments":[1,2,3]}})";
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(body, h).ok());

  ASSERT_TRUE(h.paramsArguments().has_value());
  const auto& r = *h.paramsArguments();
  EXPECT_EQ(body.substr(r.start, r.end - r.start), "[1,2,3]");
}

TEST(AgentBodyHandlerTest, AbsentArgumentsIsNullopt) {
  AgentBodyHandler h;
  ASSERT_TRUE(feedOne(R"({"jsonrpc":"2.0","method":"ping","id":1,"params":{}})", h).ok());
  EXPECT_FALSE(h.paramsArguments().has_value());
}

TEST(AgentBodyHandlerTest, DuplicateRootKeyAborts) {
  AgentBodyHandler h;
  auto s = feedOne(R"({"jsonrpc":"2.0","method":"ping","method":"pong","id":1})", h);
  EXPECT_FALSE(s.ok());
  EXPECT_TRUE(h.hasDuplicateKeys());
}

TEST(AgentBodyHandlerTest, DuplicateParamsKeyAborts) {
  AgentBodyHandler h;
  auto s = feedOne(
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"a","name":"b"}})", h);
  EXPECT_FALSE(s.ok());
  EXPECT_TRUE(h.hasDuplicateKeys());
}

TEST(AgentBodyHandlerTest, DuplicateMetaKeyAborts) {
  AgentBodyHandler h;
  auto s = feedOne(
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"_meta":{"traceparent":"x","traceparent":"y"}}})",
      h);
  EXPECT_FALSE(s.ok());
  EXPECT_TRUE(h.hasDuplicateKeys());
}

TEST(AgentBodyHandlerTest, ChunkedFeeding) {
  AgentBodyHandler h;
  const std::string body =
      R"({"jsonrpc":"2.0","method":"tools/call","id":"req-1","params":{"name":"get_weather","arguments":{"city":"London"},"_meta":{"traceparent":"00-trace-span-01"}}})";
  ASSERT_TRUE(feedChunked(body, h, /*chunk_size=*/6).ok());
  EXPECT_EQ(h.jsonrpc(), "2.0");
  EXPECT_EQ(h.method(), "tools/call");
  EXPECT_EQ(h.id(), "req-1");
  EXPECT_EQ(h.paramsName(), "get_weather");
  EXPECT_EQ(h.metaTraceparent(), "00-trace-span-01");
  ASSERT_TRUE(h.paramsArguments().has_value());
}

} // namespace
} // namespace Wuffs
} // namespace Json
} // namespace Envoy
