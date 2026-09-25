#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Transcoder {
namespace {

using HttpFilters::AiProtocolManager::AiFilter;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiFilterSharedPtr;
using HttpFilters::AiProtocolManager::AiRequestPropagator;
using HttpFilters::AiProtocolManager::AiRequestPtr;
using HttpFilters::AiProtocolManager::AiRequestReceiver;
using HttpFilters::AiProtocolManager::AiResponseStreamPropagator;
using HttpFilters::AiProtocolManager::AiResponseStreamReceiver;
using HttpFilters::AiProtocolManager::BufferManager;
using HttpFilters::AiProtocolManager::FakeBridge;
using HttpFilters::AiProtocolManager::FilterManager;
using HttpFilters::AiProtocolManager::FlattenJsonField;
using HttpFilters::AiProtocolManager::InMemoryExternalBufferFactory;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::SseStreamPropagator;
using HttpFilters::AiProtocolManager::SseStreamReceiver;
using HttpFilters::AiProtocolManager::TranscodingEngine;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;

// Intermediate AI filter that sits between `TO_IR` and `FROM_IR` and verifies that the payload
// passing through the middle of the filter chain is in canonical IR (OpenAI Chat Completions).
class IrInspectingAiFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());
    observed_decode_ir_ = request->json();
    // Mutate the canonical IR temperature so the downstream FROM_IR filter translates it into
    // the target dialect's schema.
    request->json()["temperature"] = 0.5;
    co_return co_await std::move(propagate_request)(std::move(request));
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive_frame,
                                          SseStreamPropagator propagate_frame) override {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto frame, co_await receive_frame());
      if (!frame.has_value()) {
        co_return absl::OkStatus();
      }
      if ((*frame)->is_json()) {
        observed_encode_ir_ = (*frame)->json().json();
      }
      CO_RETURN_IF_ERROR(co_await propagate_frame(std::move(*frame)));
    }
  }

  Coroutine::Task<absl::Status> encodeUnary(AiResponseStreamReceiver receive_batch,
                                            AiResponseStreamPropagator propagate_batch) override {
    while (true) {
      ASSIGN_OR_CO_RETURN(std::vector<FlattenJsonField> batch, co_await receive_batch());
      if (batch.empty()) {
        break;
      }
      seen_unary_batches_++;
      CO_RETURN_IF_ERROR(co_await propagate_batch(std::move(batch)));
    }
    co_return absl::OkStatus();
  }

  nlohmann::json observed_decode_ir_;
  nlohmann::json observed_encode_ir_;
  size_t seen_unary_batches_{0};
};

class TranscoderFilterTest : public testing::Test {
public:
  TranscoderFilterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_), buffer_manager_(BufferManager::Config{}, factory_, bridge_),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {
    TranscoderFilter::setTargetProtocol(LLMProtocol::Unspecified);
  }

  ~TranscoderFilterTest() override {
    TranscoderFilter::setTargetProtocol(LLMProtocol::Unspecified);
    buffer_manager_.onDestroy();
  }

  TranscoderFilterConfigSharedPtr makeConfig(
      TranscoderProto::Direction request_handling,
      TranscoderProto::Direction response_handling = TranscoderProto::DIRECTION_UNSPECIFIED) {
    TranscoderProto proto;
    proto.set_request_handling(request_handling);
    proto.set_response_handling(response_handling);
    absl::StatusOr<TranscodingEngine> engine = TranscodingEngine::createDefault();
    EXPECT_TRUE(engine.ok()) << engine.status();
    return std::make_shared<const TranscoderFilterConfig>(proto, std::move(*engine),
                                                          *stats_store_.rootScope());
  }

  void runDecodeChain(std::vector<AiFilterSharedPtr> filters, const std::string& payload) {
    JsonWithExtBuf doc;
    doc.setJson(nlohmann::json::parse(payload));

    FilterManager manager(std::move(filters));
    manager.startRequest(
        std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
        [this](absl::Status s) {
          status_ = std::move(s);
          completed_ = true;
        },
        /*request_headers=*/nullptr,
        [this](Http::Code code, std::string details) {
          local_reply_code_ = code;
          local_reply_details_ = std::move(details);
        });
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_TRUE(completed_);
  }

  void runSingleDecode(TranscoderProto::Direction request_handling, LLMProtocol source_protocol,
                       const std::string& payload,
                       absl::string_view path = "/v1/chat/completions") {
    request_headers_ =
        Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", std::string(path)}};
    std::vector<AiFilterSharedPtr> filters;
    filters.push_back(std::make_shared<TranscoderFilter>(
        makeConfig(request_handling),
        AiFilterContext{stream_info_, request_headers_, source_protocol}));
    runDecodeChain(std::move(filters), payload);
  }

  nlohmann::json forwarded() { return nlohmann::json::parse(bridge_.injected_.toString()); }

  uint64_t counterValue(const std::string& name) {
    const auto counter =
        TestUtility::findCounter(stats_store_, "ai_protocol_manager.transcoder." + name);
    return counter != nullptr ? counter->value() : 0;
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge bridge_;
  BufferManager buffer_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Http::TestRequestHeaderMapImpl request_headers_;

  absl::Status status_;
  bool completed_{false};
  Http::Code local_reply_code_{Http::Code::OK};
  std::string local_reply_details_;
};

// `TO_IR` converts an Anthropic request into the canonical OpenAI Chat Completions IR.
TEST_F(TranscoderFilterTest, ToIrConvertsAnthropicRequestToCanonicalIr) {
  runSingleDecode(TranscoderProto::TO_IR, LLMProtocol::AnthropicMessages, R"({
    "model": "claude-sonnet-4-5",
    "max_tokens": 1024,
    "system": "Be concise.",
    "messages": [{"role": "user", "content": "Hello!"}]
  })");

  ASSERT_TRUE(status_.ok()) << status_;
  const nlohmann::json out = forwarded();
  EXPECT_EQ(out["model"], "claude-sonnet-4-5");
  EXPECT_EQ(out["max_completion_tokens"], 1024);
  ASSERT_EQ(out["messages"].size(), 2);
  EXPECT_EQ(out["messages"][0]["role"], "system");
  EXPECT_EQ(out["messages"][0]["content"], "Be concise.");
  EXPECT_EQ(out["messages"][1]["role"], "user");
  EXPECT_EQ(out["messages"][1]["content"], "Hello!");
  EXPECT_EQ(counterValue("transcoded"), 1);
}

// `TO_IR` lifts Gemini's model from `:path` (`/v1beta/models/{model}:generateContent`) into
// `json["model"]` and converts the payload to canonical IR.
TEST_F(TranscoderFilterTest, ToIrLiftsGeminiModelFromPathAndConvertsToIr) {
  runSingleDecode(TranscoderProto::TO_IR, LLMProtocol::GeminiGenerateContent, R"({
    "contents": [{"role": "user", "parts": [{"text": "Hello Gemini!"}]}]
  })",
                  "/v1beta/models/gemini-2.5-pro:generateContent");

  ASSERT_TRUE(status_.ok()) << status_;
  const nlohmann::json out = forwarded();
  EXPECT_EQ(out["model"], "gemini-2.5-pro");
  ASSERT_EQ(out["messages"].size(), 1);
  EXPECT_EQ(out["messages"][0]["role"], "user");
  EXPECT_EQ(out["messages"][0]["content"], "Hello Gemini!");
  EXPECT_EQ(counterValue("transcoded"), 1);
}

// `TO_IR` also lifts the streaming mode, so a `FROM_IR` leg that rebuilds the path keeps it.
TEST_F(TranscoderFilterTest, ToIrLiftsGeminiStreamingModeFromPath) {
  runSingleDecode(TranscoderProto::TO_IR, LLMProtocol::GeminiGenerateContent, R"({
    "contents": [{"role": "user", "parts": [{"text": "Hello Gemini!"}]}]
  })",
                  "/v1beta/models/gemini-2.5-flash:streamGenerateContent?alt=sse");

  ASSERT_TRUE(status_.ok()) << status_;
  nlohmann::json out = forwarded();
  EXPECT_EQ(out["model"], "gemini-2.5-flash");
  EXPECT_EQ(out["stream"], true);
}

// `FROM_IR` to Gemini moves `model` and `stream` into `:path` and drops `stream_options`, none of
// which Gemini accepts in the body.
TEST_F(TranscoderFilterTest, FromIrMovesModelAndStreamIntoGeminiPath) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);

  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions, R"({
    "model": "gemini-2.5-flash",
    "stream": true,
    "stream_options": {"include_usage": true},
    "messages": [{"role": "user", "content": "Hello!"}]
  })");

  ASSERT_TRUE(status_.ok()) << status_;
  EXPECT_EQ(request_headers_.getPathValue(),
            "/v1beta/models/gemini-2.5-flash:streamGenerateContent?alt=sse");
  nlohmann::json out = forwarded();
  EXPECT_FALSE(out.contains("model"));
  EXPECT_FALSE(out.contains("stream"));
  EXPECT_FALSE(out.contains("stream_options"));
  EXPECT_EQ(out["contents"][0]["parts"][0]["text"], "Hello!");
  EXPECT_EQ(counterValue("transcoded"), 1);
}

// A Gemini client through both legs keeps its model and streaming mode.
TEST_F(TranscoderFilterTest, GeminiStreamingRequestRoundTripsThroughBothLegs) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);
  request_headers_ = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/v1beta/models/gemini-2.5-flash:streamGenerateContent"}};

  const AiFilterContext context{stream_info_, request_headers_, LLMProtocol::GeminiGenerateContent};
  runDecodeChain(
      {std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::TO_IR), context),
       std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::FROM_IR), context)},
      R"({"contents": [{"role": "user", "parts": [{"text": "Hi"}]}]})");

  ASSERT_TRUE(status_.ok()) << status_;
  EXPECT_EQ(request_headers_.getPathValue(),
            "/v1beta/models/gemini-2.5-flash:streamGenerateContent?alt=sse");
  nlohmann::json out = forwarded();
  EXPECT_FALSE(out.contains("model"));
  EXPECT_FALSE(out.contains("stream"));
  EXPECT_EQ(out["contents"][0]["parts"][0]["text"], "Hi");
}

TEST_F(TranscoderFilterTest, FromIrUsesGenerateContentWhenNotStreaming) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);

  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions, R"({
    "model": "gemini-2.5-flash",
    "stream": false,
    "messages": [{"role": "user", "content": "Hello!"}]
  })");

  ASSERT_TRUE(status_.ok()) << status_;
  EXPECT_EQ(request_headers_.getPathValue(), "/v1beta/models/gemini-2.5-flash:generateContent");
  EXPECT_FALSE(forwarded().contains("stream"));
}

TEST_F(TranscoderFilterTest, FromIrRejectsModelThatIsNotAGeminiPathSegment) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);

  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions, R"({
    "model": "../gemini-2.5-flash:generateContent?key=x#",
    "messages": [{"role": "user", "content": "Hello!"}]
  })");

  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_THAT(local_reply_details_, testing::HasSubstr("model id"));
  EXPECT_EQ(request_headers_.getPathValue(), "/v1/chat/completions");
  EXPECT_EQ(counterValue("failed"), 1);
}

// `FROM_IR` reads the static target backend protocol and rewrites the IR payload into the backend
// schema (Anthropic Messages).
TEST_F(TranscoderFilterTest, FromIrRewritesCanonicalPayloadToTargetProtocol) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::AnthropicMessages);

  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions, R"({
    "model": "claude-sonnet-4-5",
    "max_completion_tokens": 2048,
    "messages": [
      {"role": "system", "content": "Be concise."},
      {"role": "user", "content": "Hello!"}
    ]
  })");

  ASSERT_TRUE(status_.ok()) << status_;
  const nlohmann::json out = forwarded();
  EXPECT_EQ(out["system"], "Be concise.");
  EXPECT_EQ(out["max_tokens"], 2048);
  EXPECT_FALSE(out.contains("max_completion_tokens"));
  EXPECT_EQ(counterValue("transcoded"), 1);
  EXPECT_EQ(counterValue("unresolved"), 0);
  EXPECT_EQ(counterValue("failed"), 0);
}

// Full two-instance decode pipeline:
// Client (Gemini) -> Transcoder(request_handling: TO_IR) -> IrInspectingAiFilter ->
// Transcoder(request_handling: FROM_IR) -> Backend (Anthropic).
TEST_F(TranscoderFilterTest, TwoInstanceDecodeChainTranscodesToIrAppliesAiFilterAndFromIr) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::AnthropicMessages);
  request_headers_ = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/v1beta/models/claude-sonnet-4-5:generateContent"}};

  const AiFilterContext context{stream_info_, request_headers_, LLMProtocol::GeminiGenerateContent};
  auto to_ir_filter =
      std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::TO_IR), context);
  auto mid_filter = std::make_shared<IrInspectingAiFilter>();
  auto from_ir_filter =
      std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::FROM_IR), context);

  runDecodeChain({to_ir_filter, mid_filter, from_ir_filter}, R"({
    "systemInstruction": {"parts": [{"text": "System prompt."}]},
    "generationConfig": {"maxOutputTokens": 512},
    "contents": [{"role": "user", "parts": [{"text": "Hi there!"}]}]
  })");

  ASSERT_TRUE(status_.ok()) << status_;

  // 1. Verify the intermediate AI filter received the canonical OpenAI Chat Completions IR.
  EXPECT_EQ(mid_filter->observed_decode_ir_["model"], "claude-sonnet-4-5");
  EXPECT_EQ(mid_filter->observed_decode_ir_["max_completion_tokens"], 512);
  ASSERT_EQ(mid_filter->observed_decode_ir_["messages"].size(), 2);
  EXPECT_EQ(mid_filter->observed_decode_ir_["messages"][0]["role"], "system");

  // 2. Verify the final upstream payload is in Anthropic's schema and includes the intermediate
  // filter's mutation (`temperature: 0.5`).
  const nlohmann::json out = forwarded();
  EXPECT_EQ(out["model"], "claude-sonnet-4-5");
  EXPECT_EQ(out["system"], "System prompt.");
  EXPECT_EQ(out["max_tokens"], 512);
  EXPECT_DOUBLE_EQ(out["temperature"].get<double>(), 0.5);
  ASSERT_EQ(out["messages"].size(), 1);
  EXPECT_EQ(out["messages"][0]["role"], "user");
  EXPECT_EQ(out["messages"][0]["content"], "Hi there!");
  EXPECT_EQ(counterValue("transcoded"), 2);
}

// Bidirectional unary response transcoding:
// FilterManager runs the response chain in reverse order (`N-1 .. 0`).
// With `[client_boundary_filter(request: TO_IR, response: FROM_IR), mid_filter,
// backend_boundary_filter(request: FROM_IR, response: TO_IR)]`, the response from the Gemini
// backend (`targetProtocol() == GeminiGenerateContent`) first hits `backend_boundary_filter`
// (converting Gemini -> OpenAI IR via `response_handling: TO_IR`), then passes through
// `mid_filter`, and finally hits `client_boundary_filter` (converting OpenAI IR -> Anthropic
// Messages for the client via `response_handling: FROM_IR`).
TEST_F(TranscoderFilterTest, EncodeUnaryTranscodesGeminiResponseToIrAndFromIrToAnthropic) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);
  request_headers_ = Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/messages"}};

  const AiFilterContext context{stream_info_, request_headers_, LLMProtocol::AnthropicMessages};
  auto client_boundary_filter = std::make_shared<TranscoderFilter>(
      makeConfig(TranscoderProto::TO_IR, TranscoderProto::FROM_IR), context);
  auto mid_filter = std::make_shared<IrInspectingAiFilter>();
  auto backend_boundary_filter = std::make_shared<TranscoderFilter>(
      makeConfig(TranscoderProto::FROM_IR, TranscoderProto::TO_IR), context);

  FilterManager manager({client_boundary_filter, mid_filter, backend_boundary_filter});
  FakeBridge resp_bridge(*dispatcher_);
  BufferManager resp_out_buffer(BufferManager::Config{}, factory_, resp_bridge);
  absl::Status resp_status;
  bool resp_done = false;

  manager.startUnaryResponse(factory_, resp_bridge, resp_out_buffer, [&](absl::Status s) {
    resp_status = std::move(s);
    resp_done = true;
  });

  const std::string response =
      R"({"candidates":[{"content":{"role":"model","parts":[{"text":"Hello back!"}]},)"
      R"("finishReason":"STOP"}],"modelVersion":"gemini-2.5-pro",)"
      R"("usageMetadata":{"promptTokenCount":12,"candidatesTokenCount":30,"totalTokenCount":42}})";
  Buffer::OwnedImpl body(response);
  manager.onResponseData(body, /*end_stream=*/true);
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ASSERT_TRUE(resp_done);
  ASSERT_TRUE(resp_status.ok()) << resp_status;
  EXPECT_GT(mid_filter->seen_unary_batches_, 0);

  const nlohmann::json out = nlohmann::json::parse(resp_bridge.injected_.toString());
  EXPECT_EQ(out["type"], "message");
  EXPECT_EQ(out["role"], "assistant");
  EXPECT_EQ(out["model"], "gemini-2.5-pro");
  EXPECT_EQ(out["stop_reason"], "end_turn");
  ASSERT_EQ(out["content"].size(), 1);
  EXPECT_EQ(out["content"][0]["type"], "text");
  EXPECT_EQ(out["content"][0]["text"], "Hello back!");
  EXPECT_EQ(out["usage"]["input_tokens"], 12);
  EXPECT_EQ(out["usage"]["output_tokens"], 30);
  EXPECT_EQ(counterValue("transcoded"), 2);
  EXPECT_EQ(counterValue("failed"), 0);
  resp_out_buffer.onDestroy();
}

// Bidirectional streaming SSE response transcoding:
// With `[client_boundary_filter(request: TO_IR, response: FROM_IR), mid_filter,
// backend_boundary_filter(request: FROM_IR, response: TO_IR)]`, Anthropic SSE frames from the
// backend (`targetProtocol() == AnthropicMessages`) first hit `backend_boundary_filter`
// (`response_handling: TO_IR` at backend boundary, converting Anthropic -> OpenAI
// `chat.completion.chunk` IR frames), pass through `mid_filter`, and finally hit
// `client_boundary_filter` (`response_handling: FROM_IR` at client boundary, converting OpenAI IR
// -> Gemini SSE frames for `source_protocol_ == GeminiGenerateContent`, dropping `[DONE]`).
TEST_F(TranscoderFilterTest, EncodeSseTranscodesAnthropicSseToIrAndFromIrToGemini) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::AnthropicMessages);
  request_headers_ = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/v1beta/models/gemini-2.5-pro:streamGenerateContent"}};

  const AiFilterContext context{stream_info_, request_headers_, LLMProtocol::GeminiGenerateContent};
  auto client_boundary_filter = std::make_shared<TranscoderFilter>(
      makeConfig(TranscoderProto::TO_IR, TranscoderProto::FROM_IR), context);
  auto mid_filter = std::make_shared<IrInspectingAiFilter>();
  auto backend_boundary_filter = std::make_shared<TranscoderFilter>(
      makeConfig(TranscoderProto::FROM_IR, TranscoderProto::TO_IR), context);

  FilterManager manager({client_boundary_filter, mid_filter, backend_boundary_filter});
  FakeBridge resp_bridge(*dispatcher_);
  BufferManager resp_out_buffer(BufferManager::Config{}, factory_, resp_bridge);
  absl::Status resp_status;
  bool resp_done = false;

  manager.startSseResponse(factory_, resp_bridge, resp_out_buffer, [&](absl::Status s) {
    resp_status = std::move(s);
    resp_done = true;
  });

  Buffer::OwnedImpl sse_data(
      "event: message_start\n"
      R"(data: {"type":"message_start","message":{"id":"msg_1","model":"claude-sonnet-4-5"}})"
      "\n\n"
      "event: content_block_delta\n"
      R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"chunk"}})"
      "\n\n"
      "event: message_stop\n"
      R"(data: {"type":"message_stop"})"
      "\n\n");
  manager.onResponseData(sse_data, /*end_stream=*/true);
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ASSERT_TRUE(resp_done);
  ASSERT_TRUE(resp_status.ok()) << resp_status;
  EXPECT_EQ(mid_filter->observed_encode_ir_["object"], "chat.completion.chunk");
  EXPECT_THAT(resp_bridge.injected_.toString(), testing::HasSubstr("\"candidates\""));
  EXPECT_THAT(resp_bridge.injected_.toString(), testing::HasSubstr("chunk"));
  EXPECT_THAT(resp_bridge.injected_.toString(), testing::Not(testing::HasSubstr("[DONE]")));
  EXPECT_EQ(counterValue("failed"), 0);
  resp_out_buffer.onDestroy();
}

// Gemini's candidate and prompt counts exclude thought and tool-use prompt tokens, which the IR's
// inclusive counts carry.
TEST_F(TranscoderFilterTest, EncodeUnaryCountsGeminiThoughtsAsCompletionTokens) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  const AiFilterContext context{stream_info_, request_headers_, LLMProtocol::OpenAiChatCompletions};
  auto backend_boundary_filter = std::make_shared<TranscoderFilter>(
      makeConfig(TranscoderProto::FROM_IR, TranscoderProto::TO_IR), context);

  FilterManager manager({backend_boundary_filter});
  FakeBridge resp_bridge(*dispatcher_);
  BufferManager resp_out_buffer(BufferManager::Config{}, factory_, resp_bridge);
  absl::Status resp_status;
  bool resp_done = false;
  manager.startUnaryResponse(factory_, resp_bridge, resp_out_buffer, [&](absl::Status s) {
    resp_status = std::move(s);
    resp_done = true;
  });

  Buffer::OwnedImpl body(
      R"({"candidates":[{"content":{"role":"model","parts":[{"text":"Paris"}]},)"
      R"("finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":10,"candidatesTokenCount":1,)"
      R"("thoughtsTokenCount":24,"toolUsePromptTokenCount":5,"cachedContentTokenCount":4,)"
      R"("totalTokenCount":40}})");
  manager.onResponseData(body, /*end_stream=*/true);
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ASSERT_TRUE(resp_done);
  ASSERT_TRUE(resp_status.ok()) << resp_status;
  nlohmann::json usage = nlohmann::json::parse(resp_bridge.injected_.toString())["usage"];
  EXPECT_EQ(usage["prompt_tokens"], 15);
  EXPECT_EQ(usage["completion_tokens"], 25);
  EXPECT_EQ(usage["total_tokens"], 40);
  EXPECT_EQ(usage["prompt_tokens_details"]["cached_tokens"], 4);
  EXPECT_EQ(usage["completion_tokens_details"]["reasoning_tokens"], 24);
  resp_out_buffer.onDestroy();
}

// A Gemini SSE chunk whose text is past the inline string threshold reaches the filter as an
// external reference rather than a string; it must still come out as the chunk's delta.
TEST_F(TranscoderFilterTest, EncodeSseKeepsGeminiTextHeldByReference) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  const AiFilterContext context{stream_info_, request_headers_, LLMProtocol::OpenAiChatCompletions};
  auto backend_boundary_filter = std::make_shared<TranscoderFilter>(
      makeConfig(TranscoderProto::FROM_IR, TranscoderProto::TO_IR), context);

  FilterManager manager({backend_boundary_filter});
  FakeBridge resp_bridge(*dispatcher_);
  BufferManager resp_out_buffer(BufferManager::Config{}, factory_, resp_bridge);
  absl::Status resp_status;
  bool resp_done = false;
  manager.startSseResponse(factory_, resp_bridge, resp_out_buffer, [&](absl::Status s) {
    resp_status = std::move(s);
    resp_done = true;
  });

  const std::string text = std::string(3000, 'a') + "\\n" + std::string(10, 'b');
  Buffer::OwnedImpl sse_data(absl::StrCat(
      R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":")", text,
      R"("}]},"finishReason":"STOP"}],"modelVersion":"gemini-2.5-flash"})", "\r\n\r\n"));
  manager.onResponseData(sse_data, /*end_stream=*/true);
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ASSERT_TRUE(resp_done);
  ASSERT_TRUE(resp_status.ok()) << resp_status;
  const std::string out = resp_bridge.injected_.toString();
  const std::string first_payload = out.substr(6, out.find('\n') - 6);
  nlohmann::json chunk = nlohmann::json::parse(first_payload);
  EXPECT_EQ(chunk["object"], "chat.completion.chunk");
  EXPECT_EQ(chunk["choices"][0]["delta"]["content"],
            std::string(3000, 'a') + "\n" + std::string(10, 'b'));
  EXPECT_EQ(chunk["choices"][0]["finish_reason"], "stop");
  EXPECT_THAT(out, testing::HasSubstr("data: [DONE]"));
  resp_out_buffer.onDestroy();
}

// When `response_handling` is unset (`DIRECTION_UNSPECIFIED`), the filter splices out of the
// response pipeline and passes responses through untouched.
TEST_F(TranscoderFilterTest, UnsetResponseHandlingPassesResponsesThroughUntouched) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::GeminiGenerateContent);
  request_headers_ = Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/messages"}};

  const AiFilterContext context{stream_info_, request_headers_, LLMProtocol::AnthropicMessages};
  // Only `request_handling` is set; `response_handling` defaults to `DIRECTION_UNSPECIFIED`.
  auto client_boundary_filter =
      std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::TO_IR), context);
  auto backend_boundary_filter =
      std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::FROM_IR), context);

  FilterManager manager({client_boundary_filter, backend_boundary_filter});
  FakeBridge resp_bridge(*dispatcher_);
  BufferManager resp_out_buffer(BufferManager::Config{}, factory_, resp_bridge);
  absl::Status resp_status;
  bool resp_done = false;

  manager.startUnaryResponse(factory_, resp_bridge, resp_out_buffer, [&](absl::Status s) {
    resp_status = std::move(s);
    resp_done = true;
  });

  const std::string response = R"({"already_transcoded":"by_upstream_apm"})";
  Buffer::OwnedImpl body(response);
  manager.onResponseData(body, /*end_stream=*/true);
  for (int i = 0; i < 20; ++i) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ASSERT_TRUE(resp_done);
  ASSERT_TRUE(resp_status.ok()) << resp_status;
  const nlohmann::json out = nlohmann::json::parse(resp_bridge.injected_.toString());
  EXPECT_EQ(out["already_transcoded"], "by_upstream_apm");
  EXPECT_EQ(counterValue("transcoded"), 0);
  resp_out_buffer.onDestroy();
}

// Rejects `TO_IR` when the route declared no `request_protocol` (`Unspecified`).
TEST_F(TranscoderFilterTest, RejectsToIrWhenSourceProtocolIsUnspecified) {
  runSingleDecode(TranscoderProto::TO_IR, LLMProtocol::Unspecified,
                  R"({"messages":[{"role":"user","content":"hi"}]})");

  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_THAT(local_reply_details_, testing::HasSubstr("the route declared no wire API"));
  EXPECT_EQ(counterValue("unresolved"), 1);
  EXPECT_EQ(counterValue("transcoded"), 0);
}

// Rejects `FROM_IR` when the target backend protocol is `Unspecified`.
TEST_F(TranscoderFilterTest, RejectsFromIrWhenTargetProtocolIsUnspecified) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::Unspecified);
  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions,
                  R"({"model":"claude-sonnet-4-5","messages":[{"role":"user","content":"hi"}]})");

  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_THAT(local_reply_details_, testing::HasSubstr("no target backend protocol is set"));
  EXPECT_EQ(counterValue("unresolved"), 1);
  EXPECT_EQ(counterValue("transcoded"), 0);
}

// Rejects `FROM_IR` when the transcoded document violates the target schema.
TEST_F(TranscoderFilterTest, RejectsPayloadTheTargetSchemaWouldReject) {
  TranscoderFilter::setTargetProtocol(LLMProtocol::AnthropicMessages);
  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions,
                  R"({"model":"claude-sonnet-4-5"})");

  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(counterValue("failed"), 1);
  EXPECT_EQ(counterValue("transcoded"), 0);
  EXPECT_EQ(counterValue("unresolved"), 0);
}

} // namespace
} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
