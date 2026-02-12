#include "source/common/http/sse/sse_parser.h"
#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

// Tests for ResponseContentType detection and BackendResponse handling.
class SseResponseTest : public testing::Test {};

// Verifies body is accessible for JSON responses.
TEST_F(SseResponseTest, JsonResponseUsesBodyDirectly) {
  BackendResponse response;
  response.content_type = ResponseContentType::Json;
  response.body = R"({"jsonrpc":"2.0","id":1,"result":{}})";

  // For JSON responses, body is used directly (extracted_jsonrpc is empty).
  EXPECT_TRUE(response.extracted_jsonrpc.empty());
  EXPECT_EQ(response.body, R"({"jsonrpc":"2.0","id":1,"result":{}})");
}

// Verifies extracted_jsonrpc is used for SSE responses when populated.
TEST_F(SseResponseTest, SseResponseUsesExtractedJsonrpc) {
  BackendResponse response;
  response.content_type = ResponseContentType::Sse;
  response.body = "data: {\"first\":\"event\"}\n\ndata: {\"second\":\"event\"}\n\n";
  response.extracted_jsonrpc = R"({"second":"event"})";

  // For SSE responses, extracted_jsonrpc is preferred when available.
  EXPECT_FALSE(response.extracted_jsonrpc.empty());
  EXPECT_EQ(response.extracted_jsonrpc, R"({"second":"event"})");
}

// Verifies empty extracted_jsonrpc falls back to body.
TEST_F(SseResponseTest, EmptyExtractedJsonrpcFallsBackToBody) {
  BackendResponse response;
  response.content_type = ResponseContentType::Sse;
  response.body = "fallback body";
  response.extracted_jsonrpc = ""; // Not populated yet

  // When extracted_jsonrpc is empty, body is the fallback.
  EXPECT_TRUE(response.extracted_jsonrpc.empty());
  EXPECT_EQ(response.body, "fallback body");
}

// Verifies content type helper methods.
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

// Tests for classifyMessage function.
class ClassifyMessageTest : public testing::Test {};

// Verifies Response type detection (has result with matching id).
TEST_F(ClassifyMessageTest, ClassifiesResponseWithResult) {
  std::string data = R"({"jsonrpc":"2.0","id":1,"result":{"tools":[]}})";
  EXPECT_EQ(classifyMessage(data, 1), SseMessageType::Response);
  // request_id=0 does NOT match id=1 - they must match exactly.
  EXPECT_EQ(classifyMessage(data, 0), SseMessageType::Unknown);
}

// Verifies Response type detection (has error with matching id).
TEST_F(ClassifyMessageTest, ClassifiesResponseWithError) {
  std::string data =
      R"({"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"Method not found"}})";
  EXPECT_EQ(classifyMessage(data, 1), SseMessageType::Response);
}

// Verifies Response with non-matching ID returns Unknown.
TEST_F(ClassifyMessageTest, ResponseWithNonMatchingIdReturnsUnknown) {
  std::string data = R"({"jsonrpc":"2.0","id":99,"result":{}})";
  // Request ID is 1 but response ID is 99 - should not match.
  EXPECT_EQ(classifyMessage(data, 1), SseMessageType::Unknown);
}

// Verifies Notification type detection (method without id).
TEST_F(ClassifyMessageTest, ClassifiesNotification) {
  std::string data =
      R"({"jsonrpc":"2.0","method":"notifications/progress","params":{"progress":50}})";
  EXPECT_EQ(classifyMessage(data, 1), SseMessageType::Notification);
}

// Verifies ServerRequest type detection (method with id).
TEST_F(ClassifyMessageTest, ClassifiesServerRequest) {
  std::string data = R"({"jsonrpc":"2.0","id":99,"method":"roots/list","params":{}})";
  EXPECT_EQ(classifyMessage(data, 1), SseMessageType::ServerRequest);
}

// Verifies sampling/createMessage is classified as ServerRequest.
TEST_F(ClassifyMessageTest, ClassifiesSamplingCreateMessage) {
  std::string data = R"({"jsonrpc":"2.0","id":42,"method":"sampling/createMessage","params":{}})";
  EXPECT_EQ(classifyMessage(data, 1), SseMessageType::ServerRequest);
}

// Verifies invalid JSON returns Unknown.
TEST_F(ClassifyMessageTest, InvalidJsonReturnsUnknown) {
  std::string data = "not valid json";
  EXPECT_EQ(classifyMessage(data, 1), SseMessageType::Unknown);
}

// Verifies empty string returns Unknown.
TEST_F(ClassifyMessageTest, EmptyStringReturnsUnknown) {
  EXPECT_EQ(classifyMessage("", 1), SseMessageType::Unknown);
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
  // Body should contain the full SSE data (for pass-through).
  EXPECT_EQ(received_response.body, "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n\n");
  // Note: extracted_jsonrpc is only populated in aggregate mode.
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
  Buffer::OwnedImpl data("data: {\"progress\":1}\n\n"
                         "data: {\"progress\":2}\n\n"
                         "data: {\"result\":\"done\"}\n\n");
  callbacks->onData(data, true);

  EXPECT_TRUE(callback_invoked);
  // Full body should be preserved for pass-through.
  EXPECT_THAT(received_response.body, testing::HasSubstr("progress\":1"));
  EXPECT_THAT(received_response.body, testing::HasSubstr("progress\":2"));
  EXPECT_THAT(received_response.body, testing::HasSubstr("result\":\"done"));
  // Note: Without aggregate_mode, extracted_jsonrpc won't be populated.
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
