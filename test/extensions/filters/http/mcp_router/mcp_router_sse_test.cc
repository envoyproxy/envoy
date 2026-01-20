#include "source/common/http/sse/sse_parser.h"
#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

// Tests for ResponseContentType detection and BackendResponse SSE handling.
class SseResponseTest : public testing::Test {};

// Verifies getJsonRpcBody returns body for JSON responses.
TEST_F(SseResponseTest, GetJsonRpcBodyReturnsBodyForJson) {
  BackendResponse response;
  response.content_type = ResponseContentType::Json;
  response.body = R"({"jsonrpc":"2.0","id":1,"result":{}})";

  EXPECT_EQ(response.getJsonRpcBody(), R"({"jsonrpc":"2.0","id":1,"result":{}})");
}

// Verifies getJsonRpcBody returns last SSE event data for SSE responses.
TEST_F(SseResponseTest, GetJsonRpcBodyReturnsLastSseEventData) {
  BackendResponse response;
  response.content_type = ResponseContentType::Sse;
  // Body contains multiple SSE events - should return last event's data.
  response.body = "data: {\"first\":\"event\"}\n\ndata: {\"second\":\"event\"}\n\n";

  EXPECT_EQ(response.getJsonRpcBody(), R"({"second":"event"})");
}

// Verifies getJsonRpcBody returns body for empty SSE body.
TEST_F(SseResponseTest, GetJsonRpcBodyReturnsEmptyForEmptyBody) {
  BackendResponse response;
  response.content_type = ResponseContentType::Sse;
  response.body = "";

  EXPECT_EQ(response.getJsonRpcBody(), "");
}

// Verifies getJsonRpcBody falls back to body for unknown content type.
TEST_F(SseResponseTest, GetJsonRpcBodyFallsBackToBodyForUnknown) {
  BackendResponse response;
  response.content_type = ResponseContentType::Unknown;
  response.body = "fallback body";

  EXPECT_EQ(response.getJsonRpcBody(), "fallback body");
}

// Verifies getJsonRpcBody handles SSE without proper format.
TEST_F(SseResponseTest, GetJsonRpcBodyHandlesMalformedSse) {
  BackendResponse response;
  response.content_type = ResponseContentType::Sse;
  // No data: prefix - should fall back to body.
  response.body = "just some text";

  EXPECT_EQ(response.getJsonRpcBody(), "just some text");
}

// Verifies getJsonRpcBody parses SSE with complete event ending.
TEST_F(SseResponseTest, GetJsonRpcBodyParsesSseWithCompleteEvent) {
  BackendResponse response;
  response.content_type = ResponseContentType::Sse;
  // SSE event with proper double newline ending.
  response.body = "data: {\"result\":\"ok\"}\n\n";

  EXPECT_EQ(response.getJsonRpcBody(), R"({"result":"ok"})");
}

// Verifies isJson and isSse helper methods.
TEST_F(SseResponseTest, ContentTypeHelpers) {
  {
    BackendResponse response;
    response.content_type = ResponseContentType::Json;
    EXPECT_TRUE(response.isJson());
    EXPECT_FALSE(response.isSse());
  }
  {
    BackendResponse response;
    response.content_type = ResponseContentType::Sse;
    EXPECT_FALSE(response.isJson());
    EXPECT_TRUE(response.isSse());
  }
  {
    BackendResponse response;
    response.content_type = ResponseContentType::Unknown;
    EXPECT_FALSE(response.isJson());
    EXPECT_FALSE(response.isSse());
  }
}

// Tests for BackendStreamCallbacks SSE handling behavior.
class BackendStreamCallbacksSseTest : public testing::Test {};

// Verifies SSE content type is detected from response headers.
TEST_F(BackendStreamCallbacksSseTest, DetectsSseContentType) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("sse_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType("text/event-stream");
  callbacks->onHeaders(std::move(headers), false);

  // Send SSE data.
  Buffer::OwnedImpl data("data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n\n");
  callbacks->onData(data, true);

  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(received_response.isSse());
  EXPECT_EQ(received_response.content_type, ResponseContentType::Sse);
  // Body should contain the full SSE data.
  EXPECT_EQ(received_response.body, "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n\n");
  // getJsonRpcBody should parse and return first event data.
  EXPECT_EQ(received_response.getJsonRpcBody(), R"({"jsonrpc":"2.0","id":1,"result":{}})");
}

// Verifies JSON content type is detected from response headers.
TEST_F(BackendStreamCallbacksSseTest, DetectsJsonContentType) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("json_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType("application/json");
  callbacks->onHeaders(std::move(headers), false);

  Buffer::OwnedImpl data(R"({"jsonrpc":"2.0","id":1,"result":{}})");
  callbacks->onData(data, true);

  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(received_response.isJson());
  EXPECT_EQ(received_response.body, R"({"jsonrpc":"2.0","id":1,"result":{}})");
  EXPECT_EQ(received_response.getJsonRpcBody(), R"({"jsonrpc":"2.0","id":1,"result":{}})");
}

// Verifies SSE body is buffered correctly for pass-through.
TEST_F(BackendStreamCallbacksSseTest, BuffersSseBodyForPassThrough) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("sse_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType("text/event-stream");
  callbacks->onHeaders(std::move(headers), false);

  // Send multiple SSE events - should all be in body for pass-through.
  Buffer::OwnedImpl data(
      "data: {\"progress\":1}\n\n"
      "data: {\"progress\":2}\n\n"
      "data: {\"result\":\"done\"}\n\n");
  callbacks->onData(data, true);

  EXPECT_TRUE(callback_invoked);
  // Full body should be preserved for pass-through.
  EXPECT_THAT(received_response.body, testing::HasSubstr("progress\":1"));
  EXPECT_THAT(received_response.body, testing::HasSubstr("progress\":2"));
  EXPECT_THAT(received_response.body, testing::HasSubstr("result\":\"done"));
  // getJsonRpcBody returns last event data for aggregation (tools/list).
  EXPECT_EQ(received_response.getJsonRpcBody(), R"({"result":"done"})");
}

// Verifies SSE body is buffered correctly across multiple data chunks.
TEST_F(BackendStreamCallbacksSseTest, BuffersSseDataAcrossChunks) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("sse_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType("text/event-stream");
  callbacks->onHeaders(std::move(headers), false);

  // Send SSE data in chunks.
  Buffer::OwnedImpl chunk1("data: {\"partial\":");
  callbacks->onData(chunk1, false);

  Buffer::OwnedImpl chunk2("\"data\"}\n\n");
  callbacks->onData(chunk2, true);

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(received_response.body, "data: {\"partial\":\"data\"}\n\n");
  EXPECT_EQ(received_response.getJsonRpcBody(), R"({"partial":"data"})");
}

// Verifies SSE content type detection with charset parameter.
TEST_F(BackendStreamCallbacksSseTest, DetectsSseContentTypeWithCharset) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("sse_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType("text/event-stream; charset=utf-8");
  callbacks->onHeaders(std::move(headers), false);

  Buffer::OwnedImpl data("data: test\n\n");
  callbacks->onData(data, true);

  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(received_response.isSse());
}

// Verifies backend name is set correctly.
TEST_F(BackendStreamCallbacksSseTest, SetsBackendName) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("my_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType("text/event-stream");
  callbacks->onHeaders(std::move(headers), false);

  Buffer::OwnedImpl data("data: {\"test\":true}\n\n");
  callbacks->onData(data, true);

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(received_response.backend_name, "my_backend");
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
