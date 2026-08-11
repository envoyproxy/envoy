// Benchmarks for SSE response token-usage extraction. Guards the scanner's
// linear-work property (previously asserted via production counters): the
// per-byte cost must stay flat across frame sizes, and a
// content-delta-dominated stream must stay linear in stream length.
//
// Run with:
//   bazel run //test/extensions/filters/http/ai_protocol_manager:response_handler_benchmark

#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_handler.h"

#include "test/common/stats/stat_test_utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

std::string chatCompletionStream(size_t delta_events) {
  std::string stream;
  stream.reserve(delta_events * 128);
  for (size_t i = 0; i < delta_events; ++i) {
    stream += "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
              "\"choices\":[{\"delta\":{\"content\":\"hello world\"}}],\"usage\":null}\n\n";
  }
  stream += "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\",\"choices\":[],"
            "\"usage\":{\"prompt_tokens\":19,\"completion_tokens\":10,\"total_tokens\":29}}\n\n"
            "data: [DONE]\n\n";
  return stream;
}

// One content-delta-heavy stream (arg 0: number of delta events) delivered at
// a fixed fragmentation granularity (arg 1: frame size in bytes; 0 = one
// frame). Linearity shows up as flat bytes_per_second across both arguments.
void sseExtractionThroughput(benchmark::State& state) {
  const size_t delta_events = state.range(0);
  const size_t frame_size = state.range(1);
  const std::string stream = chatCompletionStream(delta_events);

  Stats::TestUtil::TestStore store;
  AiProtocolManagerStats stats{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*store.rootScope(), "benchmark."))};

  for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
    SseResponseHandler handler(ApiProtocol::Unspecified, /*max_event_size=*/1024 * 1024,
                               /*max_parsed_events=*/1 << 20, stats);
    if (frame_size == 0) {
      Buffer::OwnedImpl all(stream);
      handler.onData(all);
    } else {
      for (size_t offset = 0; offset < stream.size(); offset += frame_size) {
        Buffer::OwnedImpl frame(absl::string_view(stream).substr(offset, frame_size));
        handler.onData(frame);
      }
    }
    handler.onEndStream();
    benchmark::DoNotOptimize(handler.usage().total_tokens);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * stream.size()));
}
BENCHMARK(sseExtractionThroughput)
    ->ArgNames({"delta_events", "frame_size"})
    ->Args({100, 0})
    ->Args({100, 64})
    ->Args({100, 1})
    ->Args({10000, 0})
    ->Args({10000, 64})
    ->Args({10000, 1})
    ->Unit(benchmark::kMillisecond);

// A near-cap event delivered whole vs byte-at-a-time: the discard/skip
// decision must not change the per-byte cost profile.
void sseNearLimitEvent(benchmark::State& state) {
  const size_t frame_size = state.range(0);
  constexpr uint32_t cap = 1024 * 1024;
  const std::string stream =
      "data: {\"pad\":\"" + std::string(cap - 4096, 'x') + "\",\"usageMetadata\":{" +
      "\"promptTokenCount\":6,\"candidatesTokenCount\":149,\"totalTokenCount\":155}}\n\n";

  Stats::TestUtil::TestStore store;
  AiProtocolManagerStats stats{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*store.rootScope(), "benchmark."))};

  for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
    SseResponseHandler handler(ApiProtocol::GeminiGenerateContent, cap,
                               /*max_parsed_events=*/1 << 20, stats);
    if (frame_size == 0) {
      Buffer::OwnedImpl all(stream);
      handler.onData(all);
    } else {
      for (size_t offset = 0; offset < stream.size(); offset += frame_size) {
        Buffer::OwnedImpl frame(absl::string_view(stream).substr(offset, frame_size));
        handler.onData(frame);
      }
    }
    handler.onEndStream();
    benchmark::DoNotOptimize(handler.usage().total_tokens);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * stream.size()));
}
BENCHMARK(sseNearLimitEvent)
    ->ArgName("frame_size")
    ->Arg(0)
    ->Arg(4096)
    ->Arg(1)
    ->Unit(benchmark::kMillisecond);

// Anthropic-style stream dominated by named content deltas: classification
// drops them on the `event:` line, so per-delta cost must be scan-only (no
// materialization, no JSON parse).
void sseAnthropicDeltaStream(benchmark::State& state) {
  const size_t delta_events = state.range(0);
  std::string stream = "event: message_start\n"
                       "data: {\"type\":\"message_start\",\"message\":{\"model\":\"m\","
                       "\"usage\":{\"input_tokens\":9,\"output_tokens\":1}}}\n\n";
  for (size_t i = 0; i < delta_events; ++i) {
    stream += "event: content_block_delta\n"
              "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\","
              "\"text\":\"hello world hello world\"}}\n\n";
  }
  stream += "event: message_delta\n"
            "data: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":42}}\n\n"
            "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

  Stats::TestUtil::TestStore store;
  AiProtocolManagerStats stats{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*store.rootScope(), "benchmark."))};
  for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
    SseResponseHandler handler(ApiProtocol::Unspecified, /*max_event_size=*/1024 * 1024,
                               /*max_parsed_events=*/1 << 20, stats);
    Buffer::OwnedImpl all(stream);
    handler.onData(all);
    handler.onEndStream();
    benchmark::DoNotOptimize(handler.usage().output_tokens);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * stream.size()));
}
BENCHMARK(sseAnthropicDeltaStream)
    ->ArgName("delta_events")
    ->Arg(100)
    ->Arg(10000)
    ->Unit(benchmark::kMillisecond);

// Dense JSON body at the inspection cap: measures the EOS parse cost of the
// current document-tree parser (the known amplification the streaming-parser
// follow-up removes).
void jsonDenseBody(benchmark::State& state) {
  std::string body = "[";
  while (body.size() < size_t(state.range(0)) - 128) {
    body += "{\"a\":1},";
  }
  body += "{\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":149,"
          "\"totalTokenCount\":155}}]";

  Stats::TestUtil::TestStore store;
  AiProtocolManagerStats stats{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*store.rootScope(), "benchmark."))};
  for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
    JsonResponseHandler handler(ApiProtocol::Unspecified,
                                /*max_inspected_body_size=*/4 * 1024 * 1024, stats);
    Buffer::OwnedImpl all(body);
    handler.onData(all);
    handler.onEndStream();
    benchmark::DoNotOptimize(handler.usage().total_tokens);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * body.size()));
}
BENCHMARK(jsonDenseBody)
    ->ArgName("body_bytes")
    ->Arg(64 * 1024)
    ->Arg(4 * 1024 * 1024)
    ->Unit(benchmark::kMillisecond);

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
