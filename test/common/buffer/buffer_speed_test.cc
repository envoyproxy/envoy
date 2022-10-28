#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/http/stream_reset_handler.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace Envoy {

static constexpr uint64_t MaxBufferLength = 1024 * 1024;

class FakeStreamResetHandler : public Http::StreamResetHandler {
public:
  void resetStream(Http::StreamResetReason reason) override { UNREFERENCED_PARAMETER(reason); }
};

// The fragment needs to be heap allocated in order to survive past the processing done in the inner
// loop in the benchmarks below. Do not attempt to release the actual contents of the buffer.
void deleteFragment(const void*, size_t, const Buffer::BufferFragmentImpl* self) { delete self; }

// Test the creation of an empty OwnedImpl.
static void bufferCreateEmpty(benchmark::State& state) {
  uint64_t length = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Buffer::OwnedImpl buffer;
    length += buffer.length();
  }
  benchmark::DoNotOptimize(length);
}
BENCHMARK(bufferCreateEmpty);

// Test add performance of OwnedImpl vs WatermarkBuffer
static void bufferVsWatermarkBuffer(benchmark::State& state) {
  const uint64_t length = state.range(0);
  const uint64_t high_watermark = state.range(1);
  const uint64_t step = state.range(2);
  const bool use_watermark_buffer = (state.range(3) != 0);
  const std::string data(step, 'a');

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    std::unique_ptr<Buffer::Instance> buffer;
    if (use_watermark_buffer) {
      buffer = std::make_unique<Buffer::WatermarkBuffer>([]() {}, []() {}, []() {});
      buffer->setWatermarks(high_watermark);
    } else {
      buffer = std::make_unique<Buffer::OwnedImpl>();
    }

    for (uint64_t idx = 0; idx < length; idx += step) {
      buffer->add(data);
    }
  }
}
BENCHMARK(bufferVsWatermarkBuffer)
    ->Args({1024, 0, 1, 0})
    ->Args({1024, 0, 1, 1})
    ->Args({1024, 1, 1, 1})
    ->Args({1024, 1024, 1, 1})
    ->Args({64 * 1024, 0, 1, 0})
    ->Args({64 * 1024, 0, 1, 1})
    ->Args({64 * 1024, 1, 1, 1})
    ->Args({64 * 1024, 64 * 1024, 1, 1})
    ->Args({64 * 1024, 0, 32, 0})
    ->Args({64 * 1024, 64 * 1024, 32, 1})
    ->Args({1024 * 1024, 0, 1, 0})
    ->Args({1024 * 1024, 0, 1, 1})
    ->Args({1024 * 1024, 1, 1, 1})
    ->Args({1024 * 1024, 1024 * 1024, 1, 1})
    ->Args({1024 * 1024, 0, 32, 0})
    ->Args({1024 * 1024, 1024 * 1024, 32, 1});

// Measure performance impact of enabling accounts.
static void bufferAccountUse(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  uint64_t iters = state.range(1);
  const bool enable_accounts = (state.range(2) != 0);

  auto config = envoy::config::overload::v3::BufferFactoryConfig();
  config.set_minimum_account_to_track_power_of_two(2);
  Buffer::WatermarkBufferFactory buffer_factory(config);
  FakeStreamResetHandler reset_handler;
  auto account = buffer_factory.createAccount(reset_handler);
  RELEASE_ASSERT(account != nullptr, "");

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Buffer::OwnedImpl buffer;
    if (enable_accounts) {
      buffer.bindAccount(account);
    }
    for (uint64_t idx = 0; idx < iters; ++idx) {
      buffer.add(data);
    }
  }
  account->clearDownstream();
}
BENCHMARK(bufferAccountUse)
    ->Args({1, 1024 * 1024, 0})
    ->Args({1, 1024 * 1024, 1})
    ->Args({1024, 1024, 0})
    ->Args({1024, 1024, 1})
    ->Args({4 * 1024, 1024, 0})
    ->Args({4 * 1024, 1024, 1})
    ->Args({16 * 1024, 1024, 0})
    ->Args({16 * 1024, 1024, 1});

// Test the creation of an OwnedImpl with varying amounts of content.
static void bufferCreate(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  uint64_t length = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Buffer::OwnedImpl buffer(input);
    length += buffer.length();
  }
  benchmark::DoNotOptimize(length);
}
BENCHMARK(bufferCreate)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Grow an OwnedImpl in very small amounts.
static void bufferAddSmallIncrement(benchmark::State& state) {
  const std::string data("a");
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer.add(input);
    if (buffer.length() >= MaxBufferLength) {
      // Keep the test's memory usage from growing too large.
      // @note Ideally we could use state.PauseTiming()/ResumeTiming() to exclude
      // the time spent in the drain operation, but those functions themselves are
      // heavyweight enough to cloud the measurements:
      // https://github.com/google/benchmark/issues/179
      buffer.drain(buffer.length());
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferAddSmallIncrement)->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5);

// Test the appending of varying amounts of content from a string to an OwnedImpl.
static void bufferAddString(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer.add(data);
    if (buffer.length() >= MaxBufferLength) {
      buffer.drain(buffer.length());
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferAddString)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Variant of bufferAddString that appends from another Buffer::Instance
// rather than from a string.
static void bufferAddBuffer(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  const Buffer::OwnedImpl to_add(data);
  Buffer::OwnedImpl buffer(input);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer.add(to_add);
    if (buffer.length() >= MaxBufferLength) {
      buffer.drain(buffer.length());
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferAddBuffer)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the prepending of varying amounts of content from a string to an OwnedImpl.
static void bufferPrependString(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer.prepend(data);
    if (buffer.length() >= MaxBufferLength) {
      buffer.drain(buffer.length());
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferPrependString)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the prepending of one OwnedImpl to another.
static void bufferPrependBuffer(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    // The prepend method removes the content from its source buffer. To populate a new source
    // buffer every time without the overhead of a copy, we use an BufferFragment that references
    // (and never deletes) an external string.
    Buffer::OwnedImpl to_add;
    auto fragment =
        std::make_unique<Buffer::BufferFragmentImpl>(input.data(), input.size(), deleteFragment);
    to_add.addBufferFragment(*fragment.release());

    buffer.prepend(to_add);
    if (buffer.length() >= MaxBufferLength) {
      buffer.drain(input.size());
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferPrependBuffer)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

static void bufferDrain(benchmark::State& state) {
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
    UNREFERENCED_PARAMETER(_);
    buffer.add(to_add);
    buffer.drain(drain_size[drain_cycle]);
    drain_cycle++;
    drain_cycle %= DrainCycleSize;
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferDrain)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Drain an OwnedImpl in very small amounts.
static void bufferDrainSmallIncrement(benchmark::State& state) {
  const std::string data(1024 * 1024, 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer.drain(state.range(0));
    if (buffer.length() == 0) {
      buffer.add(input);
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferDrainSmallIncrement)->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5);

// Test the moving of content from one OwnedImpl to another.
static void bufferMove(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer1(input);
  Buffer::OwnedImpl buffer2(input);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer1.move(buffer2); // now buffer1 has 2 copies of the input, and buffer2 is empty.
    buffer2.move(buffer1, input.size()); // now buffer1 and buffer2 are the same size.
  }
  uint64_t length = buffer1.length();
  benchmark::DoNotOptimize(length);
}
BENCHMARK(bufferMove)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the moving of content from one OwnedImpl to another, one byte at a time, to
// exercise the (likely inefficient) code path in the implementation that handles
// partial moves.
static void bufferMovePartial(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer1(input);
  Buffer::OwnedImpl buffer2(input);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    while (buffer2.length() != 0) {
      buffer1.move(buffer2, 1);
    }
    buffer2.move(buffer1, input.size()); // now buffer1 and buffer2 are the same size.
  }
  uint64_t length = buffer1.length();
  benchmark::DoNotOptimize(length);
}
BENCHMARK(bufferMovePartial)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the reserve+commit cycle, for the special case where the reserved space is
// fully used (and therefore the commit size equals the reservation size).
static void bufferReserveCommit(benchmark::State& state) {
  Buffer::OwnedImpl buffer;
  auto size = state.range(0);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Buffer::Reservation reservation = buffer.reserveForReadWithLengthForTest(size);
    reservation.commit(reservation.length());
    if (buffer.length() >= MaxBufferLength) {
      buffer.drain(buffer.length());
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferReserveCommit)
    ->Arg(1)
    ->Arg(4 * 1024)
    ->Arg(16 * 1024)
    ->Arg(64 * 1024)
    ->Arg(128 * 1024);

// Test the reserve+commit cycle, for the common case where the reserved space is
// only partially used (and therefore the commit size is smaller than the reservation size).
static void bufferReserveCommitPartial(benchmark::State& state) {
  Buffer::OwnedImpl buffer;
  auto size = state.range(0);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Buffer::Reservation reservation = buffer.reserveForReadWithLengthForTest(size);
    // Commit one byte from the first slice and nothing from any subsequent slice.
    reservation.commit(1);
    if (buffer.length() >= MaxBufferLength) {
      buffer.drain(buffer.length());
    }
  }
  benchmark::DoNotOptimize(buffer.length());
}
BENCHMARK(bufferReserveCommitPartial)
    ->Arg(1)
    ->Arg(4 * 1024)
    ->Arg(16 * 1024)
    ->Arg(64 * 1024)
    ->Arg(128 * 1024);

// Test the linearization of a buffer in the best case where the data is in one slice.
static void bufferLinearizeSimple(benchmark::State& state) {
  const std::string data(state.range(0), 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer.drain(buffer.length());
    auto fragment =
        std::make_unique<Buffer::BufferFragmentImpl>(input.data(), input.size(), deleteFragment);
    buffer.addBufferFragment(*fragment.release());
    benchmark::DoNotOptimize(buffer.linearize(state.range(0)));
  }
}
BENCHMARK(bufferLinearizeSimple)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test the linearization of a buffer in the general case where the data is spread among
// many slices.
static void bufferLinearizeGeneral(benchmark::State& state) {
  static constexpr uint64_t SliceSize = 1024;
  const std::string data(SliceSize, 'a');
  const absl::string_view input(data);
  Buffer::OwnedImpl buffer;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    buffer.drain(buffer.length());
    do {
      auto fragment =
          std::make_unique<Buffer::BufferFragmentImpl>(input.data(), input.size(), deleteFragment);
      buffer.addBufferFragment(*fragment.release());
    } while (buffer.length() < static_cast<uint64_t>(state.range(0)));
    benchmark::DoNotOptimize(buffer.linearize(state.range(0)));
  }
}
BENCHMARK(bufferLinearizeGeneral)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test buffer search, for the simple case where there are no partial matches for
// the pattern in the buffer.
static void bufferSearch(benchmark::State& state) {
  const std::string Pattern(16, 'b');
  std::string data;
  data.reserve(state.range(0) + Pattern.length());
  data += std::string(state.range(0), 'a');
  data += Pattern;

  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  ssize_t result = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    result += buffer.search(Pattern.c_str(), Pattern.length(), 0, 0);
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(bufferSearch)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test buffer search, for the more challenging case where there are many partial matches
// for the pattern in the buffer.
static void bufferSearchPartialMatch(benchmark::State& state) {
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
    UNREFERENCED_PARAMETER(_);
    result += buffer.search(Pattern.c_str(), Pattern.length(), 0, 0);
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(bufferSearchPartialMatch)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test buffer startsWith, for the simple case where there is no match for the pattern at the start
// of the buffer.
static void bufferStartsWith(benchmark::State& state) {
  const std::string Pattern(16, 'b');
  std::string data;
  data.reserve(state.range(0) + Pattern.length());
  data += std::string(state.range(0), 'a');
  data += Pattern;

  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  ssize_t result = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    if (!buffer.startsWith({Pattern.c_str(), Pattern.length()})) {
      result++;
    }
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(bufferStartsWith)->Arg(1)->Arg(4096)->Arg(16384)->Arg(65536);

// Test buffer startsWith, when there is a match at the start of the buffer.
static void bufferStartsWithMatch(benchmark::State& state) {
  const std::string Prefix(state.range(1), 'b');
  const std::string Suffix("babbabbbabbbbabbbbbabbbbbbabbbbbbbabbbbbbbba");
  std::string data = Prefix;
  size_t num_suffixes = 1 + state.range(0) / Prefix.length();
  data.reserve(Suffix.length() * num_suffixes + Prefix.length());
  for (size_t i = 0; i < num_suffixes; i++) {
    data += Suffix;
  }

  const absl::string_view input(data);
  Buffer::OwnedImpl buffer(input);
  ssize_t result = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    if (buffer.startsWith({Prefix.c_str(), Prefix.length()})) {
      result++;
    }
  }
  benchmark::DoNotOptimize(result);
}
BENCHMARK(bufferStartsWithMatch)
    ->Args({1, 1})
    ->Args({4096, 16})
    ->Args({16384, 256})
    ->Args({65536, 4096});

static void bufferAddVsAddFragments(benchmark::State& state) {
  static constexpr size_t OwnedImplBufferType = 0;
  static constexpr size_t WatermarkBufferType = 1;

  static constexpr size_t ClassicalAddApi = 0;
  static constexpr size_t AddFragmentsApi = 1;

  static constexpr size_t Write2UnitsPerCall = 2;
  static constexpr size_t Write5UnitsPerCall = 5;

  static constexpr size_t DataSizeToWrite = 32 * 1024 * 1024;

  size_t buffer_type = state.range(0);
  size_t api_type = state.range(1);
  size_t data_unit_size = state.range(2);
  size_t write_cycle = state.range(3);

  std::string data_unit(data_unit_size, 'c');
  absl::string_view data_unit_view(data_unit);

  for (auto _ : state) { // NOLINT
    ASSERT(buffer_type == OwnedImplBufferType || buffer_type == WatermarkBufferType);
    Buffer::InstancePtr buffer_ptr =
        buffer_type == OwnedImplBufferType
            ? std::make_unique<Buffer::OwnedImpl>()
            : std::make_unique<Buffer::WatermarkBuffer>([]() {}, []() {}, []() {});
    Buffer::Instance& buffer = *buffer_ptr;

    ASSERT(api_type == ClassicalAddApi || api_type == AddFragmentsApi);
    ASSERT(write_cycle == Write2UnitsPerCall || write_cycle == Write5UnitsPerCall);
    if (api_type == ClassicalAddApi) {
      if (write_cycle == Write2UnitsPerCall) {
        for (size_t i = 0; i < DataSizeToWrite; i += Write2UnitsPerCall * data_unit_size) {
          for (size_t c = 0; c < Write2UnitsPerCall; c++) {
            buffer.add(data_unit_view);
          }
        }
      } else {
        for (size_t i = 0; i < DataSizeToWrite; i += Write5UnitsPerCall * data_unit_size) {
          for (size_t c = 0; c < Write5UnitsPerCall; c++) {
            buffer.add(data_unit_view);
          }
        }
      }
    } else {
      if (write_cycle == Write2UnitsPerCall) {
        for (size_t i = 0; i < DataSizeToWrite; i += Write2UnitsPerCall * data_unit_size) {
          buffer.addFragments({data_unit_view, data_unit_view});
        }
      } else {
        for (size_t i = 0; i < DataSizeToWrite; i += Write5UnitsPerCall * data_unit_size) {
          buffer.addFragments(
              {data_unit_view, data_unit_view, data_unit_view, data_unit_view, data_unit_view});
        }
      }
    }
  }
}

BENCHMARK(bufferAddVsAddFragments)
    ->Args({0, 0, 16, 2})
    ->Args({0, 0, 64, 2})
    ->Args({0, 0, 4096, 2})
    ->Args({0, 0, 16, 5})
    ->Args({0, 0, 64, 5})
    ->Args({0, 0, 4096, 5})
    ->Args({0, 1, 16, 2})
    ->Args({0, 1, 64, 2})
    ->Args({0, 1, 4096, 2})
    ->Args({0, 1, 16, 5})
    ->Args({0, 1, 64, 5})
    ->Args({0, 1, 4096, 5})
    ->Args({1, 0, 16, 2})
    ->Args({1, 0, 64, 2})
    ->Args({1, 0, 4096, 2})
    ->Args({1, 0, 16, 5})
    ->Args({1, 0, 64, 5})
    ->Args({1, 0, 4096, 5})
    ->Args({1, 1, 16, 2})
    ->Args({1, 1, 64, 2})
    ->Args({1, 1, 4096, 2})
    ->Args({1, 1, 16, 5})
    ->Args({1, 1, 64, 5})
    ->Args({1, 1, 4096, 5});

} // namespace Envoy
