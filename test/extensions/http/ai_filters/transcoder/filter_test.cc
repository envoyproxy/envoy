#include <cstdlib>
#include <iostream>
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
#include "source/extensions/filters/http/ai_protocol_manager/flattening_json_codec.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
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
using HttpFilters::AiProtocolManager::FieldPathSegment;
using HttpFilters::AiProtocolManager::FilterManager;
using HttpFilters::AiProtocolManager::FlatteningJsonDecoder;
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

// AI filter that consumes the whole unary response and propagates none of it, so the filter
// after it in the response chain sees the response end before any field arrives.
class DrainingAiFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());
    co_return co_await std::move(propagate_request)(std::move(request));
  }

  Coroutine::Task<absl::Status> encodeUnary(AiResponseStreamReceiver receive_batch,
                                            AiResponseStreamPropagator) override {
    while (true) {
      ASSIGN_OR_CO_RETURN(std::vector<FlattenJsonField> batch, co_await receive_batch());
      if (batch.empty()) {
        co_return absl::OkStatus();
      }
    }
  }
};

class TranscoderFilterTest : public testing::Test {
public:
  TranscoderFilterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_), buffer_manager_(BufferManager::Config{}, factory_, bridge_),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  ~TranscoderFilterTest() override { buffer_manager_.onDestroy(); }

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

  // What the AI Protocol Manager hands each filter of the stream: the client's dialect, as the
  // route's request protocol, and the backend's, as its response protocol (`target_protocol_`).
  AiFilterContext makeContext(LLMProtocol source_protocol) {
    return AiFilterContext{.stream_info = stream_info_,
                           .request_headers = request_headers_,
                           .request_protocol = source_protocol,
                           .response_protocol = target_protocol_};
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
    filters.push_back(std::make_shared<TranscoderFilter>(makeConfig(request_handling),
                                                         makeContext(source_protocol)));
    runDecodeChain(std::move(filters), payload);
  }

  nlohmann::json forwarded() { return nlohmann::json::parse(bridge_.injected_.toString()); }

  // Runs `body`, a unary response or an SSE stream, through `filters`' response chain and returns
  // what reaches the client.
  std::string runResponse(std::vector<AiFilterSharedPtr> filters, const std::string& body,
                          bool sse) {
    FilterManager manager(std::move(filters));
    FakeBridge bridge(*dispatcher_);
    BufferManager out(BufferManager::Config{}, factory_, bridge);
    absl::Status status;
    bool done = false;
    const auto on_done = [&](absl::Status s) {
      status = std::move(s);
      done = true;
    };
    if (sse) {
      manager.startSseResponse(factory_, bridge, out, on_done);
    } else {
      manager.startUnaryResponse(factory_, bridge, out, on_done);
    }
    Buffer::OwnedImpl data(body);
    manager.onResponseData(data, /*end_stream=*/true);
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_TRUE(done);
    EXPECT_TRUE(status.ok()) << status;
    const std::string wire = bridge.injected_.toString();
    out.onDestroy();
    return wire;
  }

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
  // The backend's dialect, as the route declares it for the filters `makeContext()` builds.
  LLMProtocol target_protocol_{LLMProtocol::Unspecified};

  absl::Status status_;
  bool completed_{false};
  Http::Code local_reply_code_{Http::Code::OK};
  std::string local_reply_details_;
};

// A Gemini client through both legs keeps its model and streaming mode.
TEST_F(TranscoderFilterTest, GeminiStreamingRequestRoundTripsThroughBothLegs) {
  target_protocol_ = LLMProtocol::GeminiGenerateContent;
  request_headers_ = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/v1beta/models/gemini-2.5-flash:streamGenerateContent"}};

  const AiFilterContext context = makeContext(LLMProtocol::GeminiGenerateContent);
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

TEST_F(TranscoderFilterTest, FromIrRejectsModelThatIsNotAGeminiPathSegment) {
  target_protocol_ = LLMProtocol::GeminiGenerateContent;

  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions, R"({
    "model": "../gemini-2.5-flash:generateContent?key=x#",
    "messages": [{"role": "user", "content": "Hello!"}]
  })");

  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_THAT(local_reply_details_, testing::HasSubstr("model id"));
  EXPECT_EQ(request_headers_.getPathValue(), "/v1/chat/completions");
  EXPECT_EQ(counterValue("failed"), 1);
  EXPECT_EQ(counterValue("transcoded"), 0);
  EXPECT_EQ(counterValue("unresolved"), 0);
}

// Full two-instance decode pipeline:
// Client (Gemini) -> Transcoder(request_handling: TO_IR) -> IrInspectingAiFilter ->
// Transcoder(request_handling: FROM_IR) -> Backend (Anthropic).
TEST_F(TranscoderFilterTest, TwoInstanceDecodeChainTranscodesToIrAppliesAiFilterAndFromIr) {
  target_protocol_ = LLMProtocol::AnthropicMessages;
  request_headers_ = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/v1beta/models/claude-sonnet-4-5:generateContent"}};

  const AiFilterContext context = makeContext(LLMProtocol::GeminiGenerateContent);
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

// Bidirectional unary response transcoding. The response chain runs in reverse, so a Gemini
// backend's response is converted to the IR by `backend_boundary_filter` (`response_handling:
// TO_IR`), passes `mid_filter`, and is converted to Anthropic Messages for the client by
// `client_boundary_filter` (`response_handling: FROM_IR`).
TEST_F(TranscoderFilterTest, EncodeUnaryTranscodesGeminiResponseToIrAndFromIrToAnthropic) {
  target_protocol_ = LLMProtocol::GeminiGenerateContent;
  request_headers_ = Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/messages"}};

  const AiFilterContext context = makeContext(LLMProtocol::AnthropicMessages);
  auto mid_filter = std::make_shared<IrInspectingAiFilter>();
  const std::string wire = runResponse(
      {std::make_shared<TranscoderFilter>(
           makeConfig(TranscoderProto::TO_IR, TranscoderProto::FROM_IR), context),
       mid_filter,
       std::make_shared<TranscoderFilter>(
           makeConfig(TranscoderProto::FROM_IR, TranscoderProto::TO_IR), context)},
      R"({"candidates":[{"content":{"role":"model","parts":[{"text":"Hello back!"}]},)"
      R"("finishReason":"STOP"}],"modelVersion":"gemini-2.5-pro",)"
      R"("usageMetadata":{"promptTokenCount":12,"candidatesTokenCount":30,"totalTokenCount":42}})",
      /*sse=*/false);

  EXPECT_GT(mid_filter->seen_unary_batches_, 0);
  const nlohmann::json out = nlohmann::json::parse(wire);
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
}

// Bidirectional SSE response transcoding: Anthropic frames from the backend become IR
// `chat.completion.chunk` frames for `mid_filter`, then Gemini frames for the client, which has no
// `[DONE]` terminator.
TEST_F(TranscoderFilterTest, EncodeSseTranscodesAnthropicSseToIrAndFromIrToGemini) {
  target_protocol_ = LLMProtocol::AnthropicMessages;
  request_headers_ = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/v1beta/models/gemini-2.5-pro:streamGenerateContent"}};

  const AiFilterContext context = makeContext(LLMProtocol::GeminiGenerateContent);
  auto mid_filter = std::make_shared<IrInspectingAiFilter>();
  const std::string wire = runResponse(
      {std::make_shared<TranscoderFilter>(
           makeConfig(TranscoderProto::TO_IR, TranscoderProto::FROM_IR), context),
       mid_filter,
       std::make_shared<TranscoderFilter>(
           makeConfig(TranscoderProto::FROM_IR, TranscoderProto::TO_IR), context)},
      "event: message_start\n"
      R"(data: {"type":"message_start","message":{"id":"msg_1","model":"claude-sonnet-4-5"}})"
      "\n\n"
      "event: content_block_delta\n"
      R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"chunk"}})"
      "\n\n"
      "event: message_stop\n"
      R"(data: {"type":"message_stop"})"
      "\n\n",
      /*sse=*/true);

  EXPECT_EQ(mid_filter->observed_encode_ir_["object"], "chat.completion.chunk");
  EXPECT_THAT(wire, testing::HasSubstr("\"candidates\""));
  EXPECT_THAT(wire, testing::HasSubstr("chunk"));
  EXPECT_THAT(wire, testing::Not(testing::HasSubstr("[DONE]")));
  EXPECT_EQ(counterValue("failed"), 0);
}

// A Gemini SSE chunk whose text is past the inline string threshold reaches the filter as an
// external reference rather than a string; it must still come out as the chunk's delta.
TEST_F(TranscoderFilterTest, EncodeSseKeepsGeminiTextHeldByReference) {
  target_protocol_ = LLMProtocol::GeminiGenerateContent;
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  const AiFilterContext context = makeContext(LLMProtocol::OpenAiChatCompletions);
  const std::string text = std::string(3000, 'a') + "\\n" + std::string(10, 'b');
  const std::string wire = runResponse(
      {std::make_shared<TranscoderFilter>(
          makeConfig(TranscoderProto::FROM_IR, TranscoderProto::TO_IR), context)},
      absl::StrCat(R"(data: {"candidates":[{"content":{"role":"model","parts":[{"text":")", text,
                   R"("}]},"finishReason":"STOP"}],"modelVersion":"gemini-2.5-flash"})",
                   "\r\n\r\n"),
      /*sse=*/true);

  const std::string first_payload = wire.substr(6, wire.find('\n') - 6);
  nlohmann::json chunk = nlohmann::json::parse(first_payload);
  EXPECT_EQ(chunk["object"], "chat.completion.chunk");
  EXPECT_EQ(chunk["choices"][0]["delta"]["content"],
            std::string(3000, 'a') + "\n" + std::string(10, 'b'));
  EXPECT_EQ(chunk["choices"][0]["finish_reason"], "stop");
  EXPECT_THAT(wire, testing::HasSubstr("data: [DONE]"));
}

// When `response_handling` is unset (`DIRECTION_UNSPECIFIED`), the filter splices out of the
// response pipeline and passes responses through untouched.
TEST_F(TranscoderFilterTest, UnsetResponseHandlingPassesResponsesThroughUntouched) {
  target_protocol_ = LLMProtocol::GeminiGenerateContent;
  request_headers_ = Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/messages"}};

  const AiFilterContext context = makeContext(LLMProtocol::AnthropicMessages);
  const std::string wire = runResponse(
      {std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::TO_IR), context),
       std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::FROM_IR), context)},
      R"({"already_transcoded":"by_upstream_apm"})", /*sse=*/false);

  EXPECT_EQ(nlohmann::json::parse(wire)["already_transcoded"], "by_upstream_apm");
  EXPECT_EQ(counterValue("transcoded"), 0);
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

// Rejects `FROM_IR` when the route declared no response protocol, so the target backend protocol
// is `Unspecified`.
TEST_F(TranscoderFilterTest, RejectsFromIrWhenTargetProtocolIsUnspecified) {
  runSingleDecode(TranscoderProto::FROM_IR, LLMProtocol::OpenAiChatCompletions,
                  R"({"model":"claude-sonnet-4-5","messages":[{"role":"user","content":"hi"}]})");

  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_THAT(local_reply_details_, testing::HasSubstr("no target backend protocol is set"));
  EXPECT_EQ(counterValue("unresolved"), 1);
  EXPECT_EQ(counterValue("transcoded"), 0);
}

// When `request_handling` is unset (`DIRECTION_UNSPECIFIED`), the filter forwards the request
// untouched even though it transcodes responses.
TEST_F(TranscoderFilterTest, UnsetRequestHandlingPassesRequestThroughUntouched) {
  target_protocol_ = LLMProtocol::GeminiGenerateContent;
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};
  const std::string payload =
      R"({"model":"gemini-2.5-flash","messages":[{"role":"user","content":"hi"}]})";

  runDecodeChain({std::make_shared<TranscoderFilter>(
                     makeConfig(TranscoderProto::DIRECTION_UNSPECIFIED, TranscoderProto::TO_IR),
                     makeContext(LLMProtocol::OpenAiChatCompletions))},
                 payload);

  ASSERT_TRUE(status_.ok()) << status_;
  EXPECT_EQ(local_reply_code_, Http::Code::OK);
  EXPECT_EQ(forwarded(), nlohmann::json::parse(payload));
  EXPECT_EQ(request_headers_.getPathValue(), "/v1/chat/completions");
  EXPECT_EQ(counterValue("transcoded"), 0);
  EXPECT_EQ(counterValue("failed"), 0);
  EXPECT_EQ(counterValue("unresolved"), 0);
}

// When `response_handling` is unset, an SSE response also passes through untouched.
TEST_F(TranscoderFilterTest, UnsetResponseHandlingPassesSseThroughUntouched) {
  target_protocol_ = LLMProtocol::AnthropicMessages;
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  const std::string wire = runResponse(
      {std::make_shared<TranscoderFilter>(makeConfig(TranscoderProto::FROM_IR),
                                          makeContext(LLMProtocol::OpenAiChatCompletions))},
      "event: message_stop\n"
      R"(data: {"type":"message_stop"})"
      "\n\n",
      /*sse=*/true);

  EXPECT_THAT(wire, testing::HasSubstr("event: message_stop"));
  EXPECT_THAT(wire, testing::HasSubstr(R"("type":"message_stop")"));
  EXPECT_THAT(wire, testing::Not(testing::HasSubstr("chat.completion.chunk")));
  EXPECT_EQ(counterValue("transcoded"), 0);
  EXPECT_EQ(counterValue("failed"), 0);
}

// A unary response that ends before any field reaches the transcoder (here an earlier filter in
// the response chain consumed it) has nothing to transcode, so nothing is counted or forwarded.
TEST_F(TranscoderFilterTest, EncodeUnaryWithNoFieldsForwardsNothing) {
  target_protocol_ = LLMProtocol::GeminiGenerateContent;
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  // The response chain runs in reverse, so `DrainingAiFilter` sees the backend's response first.
  const std::string wire =
      runResponse({std::make_shared<TranscoderFilter>(
                       makeConfig(TranscoderProto::DIRECTION_UNSPECIFIED, TranscoderProto::TO_IR),
                       makeContext(LLMProtocol::OpenAiChatCompletions)),
                   std::make_shared<DrainingAiFilter>()},
                  R"({"candidates":[{"content":{"role":"model","parts":[{"text":"dropped"}]}}]})",
                  /*sse=*/false);

  EXPECT_THAT(wire, testing::Not(testing::HasSubstr("dropped")));
  EXPECT_EQ(counterValue("transcoded"), 0);
  EXPECT_EQ(counterValue("failed"), 0);
}

// A response leg that cannot be resolved (the route declared no backend protocol) forwards the
// response untranslated rather than failing it.
TEST_F(TranscoderFilterTest, UnresolvedResponseLegForwardsResponseUntranslated) {
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  const std::string wire =
      runResponse({std::make_shared<TranscoderFilter>(
                      makeConfig(TranscoderProto::DIRECTION_UNSPECIFIED, TranscoderProto::TO_IR),
                      makeContext(LLMProtocol::OpenAiChatCompletions))},
                  R"({"candidates":[{"content":{"role":"model","parts":[{"text":"as is"}]}}]})",
                  /*sse=*/false);

  EXPECT_EQ(nlohmann::json::parse(wire)["candidates"][0]["content"]["parts"][0]["text"], "as is");
  EXPECT_EQ(counterValue("unresolved"), 1);
  EXPECT_EQ(counterValue("transcoded"), 0);
  EXPECT_EQ(counterValue("failed"), 0);
}

// A backend dialect with no registered transcoding pack (OpenAI Responses) refuses every SSE
// event and also refuses to end the stream: each event is forwarded untranslated, and the stream
// ends without a trailer, each refusal counted as a failure.
TEST_F(TranscoderFilterTest, EncodeSseCountsFailureWhenStreamCannotBeFinished) {
  target_protocol_ = LLMProtocol::OpenAiResponses;
  request_headers_ =
      Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  const std::string wire =
      runResponse({std::make_shared<TranscoderFilter>(
                      makeConfig(TranscoderProto::DIRECTION_UNSPECIFIED, TranscoderProto::TO_IR),
                      makeContext(LLMProtocol::OpenAiChatCompletions))},
                  "event: response.created\n"
                  R"(data: {"type":"response.created"})"
                  "\n\n",
                  /*sse=*/true);

  EXPECT_THAT(wire, testing::HasSubstr("event: response.created"));
  EXPECT_THAT(wire, testing::HasSubstr(R"("type":"response.created")"));
  EXPECT_THAT(wire, testing::Not(testing::HasSubstr("[DONE]")));
  // One refusal for the event, one for ending the stream.
  EXPECT_EQ(counterValue("failed"), 2);
  EXPECT_EQ(counterValue("transcoded"), 0);
}

TEST(UnflattenFieldsTest, JoinsPartialStringChunks) {
  const std::vector<FlattenJsonField> fields = {
      FlattenJsonField({"id"}, "msg_1"),
      FlattenJsonField({"content", size_t{0}, "text"}, "Hel", /*is_partial=*/true),
      FlattenJsonField({"content", size_t{0}, "text"}, "lo, ", /*is_partial=*/true),
      FlattenJsonField({"content", size_t{0}, "text"}, "world"),
      FlattenJsonField({"usage"}, nlohmann::json::object()),
  };
  EXPECT_EQ(unflattenFields(fields), nlohmann::json::parse(R"({
    "id": "msg_1",
    "content": [{"text": "Hello, world"}],
    "usage": {}
  })"));
}

TEST(UnflattenFieldsTest, CompleteStringReplacesRatherThanJoins) {
  // Only a partial chunk is continued: a complete string that lands where one already is (a
  // duplicate key) replaces it, as parsing the same JSON would.
  const std::vector<FlattenJsonField> fields = {
      FlattenJsonField({"model"}, "first"),
      FlattenJsonField({"model"}, "second"),
  };
  EXPECT_EQ(unflattenFields(fields), nlohmann::json::parse(R"({"model": "second"})"));
}

// ---------------------------------------------------------------------------
// Response golden tests.

// Response-side golden cases: every unary and SSE leg for every dialect, with the expected output
// the transcoder produces. The corpus lives in `testdata/response_goldens.json`.
//
// Each case runs one transcoder instance whose `response_handling` is the case's `handling`. The
// case's `dialect` is the non-IR side of that hop: the backend for `TO_IR`, the client for
// `FROM_IR`. SSE frames are written as `{"event": ..., "data": ...}`, where `data` is the parsed
// JSON payload, or a string for a payload that is not JSON (e.g. `[DONE]`).
//
// To regenerate the expected outputs after an intentional behavior change, run this test with
// `--test_env=TRANSCODER_GOLDEN_PRINT=1 --test_output=all`: every case prints its actual output on
// a `GOLDEN_ACTUAL` line, which can be copied into the corpus.
constexpr int64_t kStreamStartUnixSeconds = 1700000000;

LLMProtocol protocolFromName(absl::string_view name) {
  if (name == "OPENAI_CHAT_COMPLETIONS") {
    return LLMProtocol::OpenAiChatCompletions;
  }
  if (name == "ANTHROPIC_MESSAGES") {
    return LLMProtocol::AnthropicMessages;
  }
  if (name == "GEMINI_GENERATE_CONTENT") {
    return LLMProtocol::GeminiGenerateContent;
  }
  ADD_FAILURE() << "unknown dialect " << name;
  return LLMProtocol::Unspecified;
}

// Writes `frames` in SSE wire format.
std::string toSseWire(const nlohmann::json& frames) {
  std::string wire;
  for (const nlohmann::json& frame : frames) {
    if (frame.contains("event")) {
      absl::StrAppend(&wire, "event: ", frame["event"].get<std::string>(), "\n");
    }
    const nlohmann::json& data = frame["data"];
    absl::StrAppend(&wire, "data: ", data.is_string() ? data.get<std::string>() : data.dump(),
                    "\n\n");
  }
  return wire;
}

// Parses an SSE stream back into frames, the inverse of `toSseWire`.
nlohmann::json fromSseWire(absl::string_view raw_wire) {
  const std::string wire = absl::StrReplaceAll(raw_wire, {{"\r\n", "\n"}});
  nlohmann::json frames = nlohmann::json::array();
  for (absl::string_view block : absl::StrSplit(wire, "\n\n", absl::SkipEmpty())) {
    nlohmann::json frame = nlohmann::json::object();
    std::string data;
    bool has_data = false;
    for (absl::string_view line : absl::StrSplit(block, '\n', absl::SkipEmpty())) {
      if (absl::ConsumePrefix(&line, "event:")) {
        frame["event"] = std::string(absl::StripLeadingAsciiWhitespace(line));
      } else if (absl::ConsumePrefix(&line, "data:")) {
        absl::ConsumePrefix(&line, " ");
        if (has_data) {
          data.push_back('\n');
        }
        absl::StrAppend(&data, line);
        has_data = true;
      } else {
        frame["other"].push_back(std::string(line));
      }
    }
    if (has_data) {
      nlohmann::json parsed = nlohmann::json::parse(data, nullptr, /*allow_exceptions=*/false);
      frame["data"] = parsed.is_discarded() ? nlohmann::json(data) : std::move(parsed);
    }
    frames.push_back(std::move(frame));
  }
  return frames;
}

class TranscoderGoldenTest : public testing::Test {
public:
  TranscoderGoldenTest()
      : api_(Api::createApiForTest(time_system_)), dispatcher_(api_->allocateDispatcher("test")) {
    time_system_.setSystemTime(std::chrono::seconds(kStreamStartUnixSeconds));
  }

  // Runs one case and returns its output in the corpus's representation.
  nlohmann::json runCase(const nlohmann::json& golden) {
    const bool to_ir = golden["handling"] == "TO_IR";
    const LLMProtocol dialect = protocolFromName(golden["dialect"].get<std::string>());

    TranscoderProto proto;
    proto.set_response_handling(to_ir ? TranscoderProto::TO_IR : TranscoderProto::FROM_IR);
    absl::StatusOr<TranscodingEngine> engine = TranscodingEngine::createDefault();
    EXPECT_TRUE(engine.ok()) << engine.status();
    auto config = std::make_shared<const TranscoderFilterConfig>(proto, std::move(*engine),
                                                                 *stats_.rootScope());

    StreamInfo::StreamInfoImpl stream_info(api_->timeSource(), nullptr,
                                           StreamInfo::FilterState::LifeSpan::FilterChain);
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"}, {":path", "/"}};
    const AiFilterContext context{stream_info, request_headers,
                                  /*request_protocol=*/
                                  to_ir ? LLMProtocol::OpenAiChatCompletions : dialect,
                                  /*request_payload_bytes=*/0,
                                  /*response_protocol=*/
                                  to_ir ? dialect : LLMProtocol::OpenAiChatCompletions};

    FilterManager manager({std::make_shared<TranscoderFilter>(config, context)});
    InMemoryExternalBufferFactory factory;
    FakeBridge bridge(*dispatcher_);
    BufferManager out(BufferManager::Config{}, factory, bridge);
    absl::Status status;
    bool done = false;
    const auto on_done = [&](absl::Status s) {
      status = std::move(s);
      done = true;
    };

    const bool sse = golden["kind"] == "sse";
    if (sse) {
      manager.startSseResponse(factory, bridge, out, on_done);
    } else {
      manager.startUnaryResponse(factory, bridge, out, on_done);
    }
    Buffer::OwnedImpl body(sse ? toSseWire(golden["input"]) : golden["input"].dump());
    manager.onResponseData(body, /*end_stream=*/true);
    for (int i = 0; i < 50 && !done; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_TRUE(done);
    EXPECT_TRUE(status.ok()) << status;

    const std::string wire = bridge.injected_.toString();
    out.onDestroy();
    return sse ? fromSseWire(wire) : nlohmann::json::parse(wire);
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
};

TEST_F(TranscoderGoldenTest, ResponsesMatchGoldens) {
  const std::string corpus = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
      "test/extensions/http/ai_filters/transcoder/testdata/response_goldens.json"));
  const nlohmann::json goldens = nlohmann::json::parse(corpus);
  ASSERT_TRUE(goldens.is_array());
  ASSERT_FALSE(goldens.empty());

  const bool print = std::getenv("TRANSCODER_GOLDEN_PRINT") != nullptr;
  for (const nlohmann::json& golden : goldens) {
    const std::string name = golden["name"].get<std::string>();
    SCOPED_TRACE(name);
    const nlohmann::json actual = runCase(golden);
    if (print) {
      std::cout << "GOLDEN_ACTUAL\t" << name << "\t" << actual.dump() << std::endl;
    }
    EXPECT_EQ(actual, golden["expected"])
        << "actual:   " << actual.dump() << "\nexpected: " << golden["expected"].dump();
  }
}

// Fields as JSON, to compare and print them. Compared as dumps so that a number's type counts.
std::string describeFields(const std::vector<FlattenJsonField>& fields) {
  nlohmann::json out = nlohmann::json::array();
  for (const FlattenJsonField& field : fields) {
    nlohmann::json path = nlohmann::json::array();
    for (const FieldPathSegment& seg : field.field_path()) {
      if (absl::holds_alternative<std::string>(seg)) {
        path.push_back(absl::get<std::string>(seg));
      } else {
        path.push_back(absl::get<size_t>(seg));
      }
    }
    out.push_back({{"path", path}, {"node", field.node()}, {"partial", field.is_partial()}});
  }
  return out.dump();
}

// The filter hands the next filter `flattenJson()` of a transcoded unary body in place of what
// `FlatteningJsonDecoder` would decode from its serialization, so the two must agree exactly: on
// every document in the corpus, and on the shapes the corpus lacks.
TEST(TranscoderFlattenJsonTest, MatchesDecoderOnSerializedDocument) {
  const std::string corpus = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
      "test/extensions/http/ai_filters/transcoder/testdata/response_goldens.json"));
  std::vector<nlohmann::json> documents;
  for (const nlohmann::json& golden : nlohmann::json::parse(corpus)) {
    documents.push_back(golden["input"]);
    documents.push_back(golden["expected"]);
  }
  for (const char* edge : {
           R"({})",
           R"([])",
           R"("text")",
           R"(7)",
           R"(null)",
           R"({"a": {}, "b": [], "c": [{}, [], [[]]], "d": {"e": {"f": {}}}})",
           R"({"int": -3, "uint": 18446744073709551615, "float": 1.5, "whole_float": 2.0})",
           R"({"s": "quote \" backslash \\ newline \n tab \t unicode \u00e9 \ud83d\ude00"})",
           R"({"z": 1, "a": 2, "m": [true, false, null]})",
       }) {
    documents.push_back(nlohmann::json::parse(edge));
  }

  for (const nlohmann::json& document : documents) {
    SCOPED_TRACE(document.dump());
    FlatteningJsonDecoder decoder;
    Buffer::OwnedImpl serialized(document.dump());
    absl::StatusOr<std::vector<FlattenJsonField>> decoded =
        decoder.onData(serialized, /*end_stream=*/true);
    ASSERT_TRUE(decoded.ok()) << decoded.status();
    const std::vector<FlattenJsonField> flattened = flattenJson(document);
    EXPECT_EQ(describeFields(flattened), describeFields(*decoded));
    EXPECT_EQ(unflattenFields(flattened), document);
  }
}

} // namespace
} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
