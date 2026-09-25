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

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::BufferManager;
using HttpFilters::AiProtocolManager::FakeBridge;
using HttpFilters::AiProtocolManager::FilterManager;
using HttpFilters::AiProtocolManager::InMemoryExternalBufferFactory;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::TranscodingEngine;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;

// Response-side golden cases: every unary and SSE leg for every dialect, with the expected output
// the transcoder produces. The corpus lives in testdata/response_goldens.json.
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
    auto config =
        std::make_shared<const TranscoderFilterConfig>(proto, std::move(*engine), *stats_.rootScope());

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
    EXPECT_EQ(actual, golden["expected"]) << "actual:   " << actual.dump()
                                          << "\nexpected: " << golden["expected"].dump();
  }
}

} // namespace
} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
