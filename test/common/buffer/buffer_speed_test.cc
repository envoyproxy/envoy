#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"

#include "absl/strings/string_view.h"
#include "testing/base/public/benchmark.h"

namespace Envoy {

// Test the creation of an empty OwnedImpl.
static void BufferCreateEmpty(benchmark::State& state) {
  uint64_t length = 0;
  for (auto _ : state) {
    Buffer::OwnedImpl buffer;
    length += buffer.length();
  }
  benchmark::DoNotOptimize(length);
}
BENCHMARK(BufferCreateEmpty);

// Test the creation of an OwnedImpl with varying amounts of content.
static void BufferCreate(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  uint64_t length = 0;
  for (auto _ : state) {
    Buffer::OwnedImpl buffer(input);
    length += buffer.length();
  }
  benchmark::DoNotOptimize(length);
}
BENCHMARK(BufferCreate)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the appending of varying amounts of content from a string to an OwnedImpl.
static void BufferAddString(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  for (auto _ : state) {
    buffer.add(data);
    // Drain from the front of the Buffer to keep the test's memory usage fixed.
    // @note Ideally we could use state.PauseTiming()/ResumeTiming() to exclude
    // the time spent in the drain operation, but those functions themselves are
    // heavyweight enough to cloud the measurements:
    // https://github.com/google/benchmark/issues/179
    buffer.drain(input.size());
  }
  uint64_t length = buffer.length();
  benchmark::DoNotOptimize(length);
}
BENCHMARK(BufferAddString)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Variant of BufferAddString that appends from another Buffer::Instance
// rather than from a string.
static void BufferAddBuffer(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  const Buffer::OwnedImpl to_add(data);
  Buffer::OwnedImpl buffer(input);
  for (auto _ : state) {
    buffer.add(to_add);
    // Drain from the front of the Buffer to keep the test's memory usage fixed.
    buffer.drain(input.size());
  }
  uint64_t length = buffer.length();
  benchmark::DoNotOptimize(length);
}
BENCHMARK(BufferAddBuffer)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

static void BufferDrain(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  const Buffer::OwnedImpl to_add(data);
  Buffer::OwnedImpl buffer(input);

  // On each iteration of the benchmark, add N bytes and drain a multiple of N, as specified
  // by DrainCycleRatios. This exercises full-slice, partial-slice, and multi-slice code paths
  // in the Buffer's drain implementation.
  constexpr size_t DrainCycleSize = 7;
  constexpr double DrainCycleRatios[DrainCycleSize] = {0.0, 1.5, 1, 1.5, 0, 2.0, 1.0};
  uint64_t drain_size[DrainCycleSize];
  for (size_t i = 0; i < DrainCycleSize; i++) {
    drain_size[i] = state.range(0) * DrainCycleRatios[i];
  }

  size_t drain_cycle = 0;
  for (auto _ : state) {
    buffer.add(to_add);
    buffer.drain(drain_size[drain_cycle]);
    drain_cycle++;
    drain_cycle %= DrainCycleSize;
  }
  uint64_t length = buffer.length();
  benchmark::DoNotOptimize(length);
}
BENCHMARK(BufferDrain)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the moving of content from one OwnedImpl to another.
static void BufferMove(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer1(input);
  Buffer::OwnedImpl buffer2(input);
  for (auto _ : state) {
    buffer1.move(buffer2); // now buffer1 has 2 copies of the input, and buffer2 is empty.
    buffer2.move(buffer1, input.size()); // now buffer1 and buffer2 are the same size.
  }
  uint64_t length = buffer1.length();
  benchmark::DoNotOptimize(length);
}
BENCHMARK(BufferMove)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the moving of content from one OwnedImpl to another, one byte at a time, to
// exercise the (likely inefficient) code path in the implementation that handles
// partial moves.
static void BufferMovePartial(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer1(input);
  Buffer::OwnedImpl buffer2(input);
  for (auto _ : state) {
    while (buffer2.length() != 0) {
      buffer1.move(buffer2, 1);
    }
    buffer2.move(buffer1, input.size()); // now buffer1 and buffer2 are the same size.
  }
  uint64_t length = buffer1.length();
  benchmark::DoNotOptimize(length);
}
BENCHMARK(BufferMovePartial)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the reserve+commit cycle, for the special case where the reserved space is
// fully used (and therefore the commit size equals the reservation size).
static void BufferReserveCommit(benchmark::State& state) {
  Buffer::OwnedImpl buffer;
  for (auto _ : state) {
    constexpr uint64_t NumSlices = 2;
    Buffer::RawSlice slices[NumSlices];
    uint64_t slices_used = buffer.reserve(state.range(0), slices, NumSlices);
    uint64_t bytes_to_commit = 0;
    for (uint64_t i = 0; i < slices_used; i++) {
      bytes_to_commit += static_cast<uint64_t>(slices[i].len_);
    }
    buffer.commit(slices, slices_used);
    buffer.drain(bytes_to_commit);
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(BufferReserveCommit)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the reserve+commit cycle, for the common case where the reserved space is
// only partially used (and therefore the commit size is smaller than the reservation size).
static void BufferReserveCommitPartial(benchmark::State& state) {
  Buffer::OwnedImpl buffer;
  for (auto _ : state) {
    constexpr uint64_t NumSlices = 2;
    Buffer::RawSlice slices[NumSlices];
    uint64_t slices_used = buffer.reserve(state.range(0), slices, NumSlices);
    ASSERT(slices_used > 0);
    // Commit one byte from the first slice and nothing from any subsequent slice.
    uint64_t bytes_to_commit = 1;
    slices[0].len_ = bytes_to_commit;
    buffer.commit(slices, 1);
    buffer.drain(bytes_to_commit);
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(BufferReserveCommitPartial)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test buffer search, for the simple case where there are no partial matches for
// the pattern in the buffer.
static void BufferSearch(benchmark::State& state) {
  const std::string Pattern(16, 'b');
  std::string data;
  data.reserve(state.range(0) + Pattern.length());
  data += std::string(state.range(0), 'a');
  data += Pattern;

  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  ssize_t result = 0;
  for (auto _ : state) {
    result += buffer.search(Pattern.c_str(), Pattern.length(), 0);
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(BufferSearch)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test buffer search, for the more challenging case where there are many partial matches
// for the pattern in the buffer.
static void BufferSearchPartialMatch(benchmark::State& state) {
  const std::string Pattern(16, 'b');
  const std::string PartialMatch("babbabbbabbbbabbbbbabbbbbbabbbbbbbabbbbbbbba");
  std::string data;
  size_t num_partial_matches = 1 + state.range(0) / PartialMatch.length();
  data.reserve(state.range(0) * num_partial_matches + Pattern.length());
  for (size_t i = 0; i < num_partial_matches; i++) {
    data += PartialMatch;
  }
  data += Pattern;

  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  ssize_t result = 0;
  for (auto _ : state) {
    result += buffer.search(Pattern.c_str(), Pattern.length(), 0);
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(BufferSearchPartialMatch)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
