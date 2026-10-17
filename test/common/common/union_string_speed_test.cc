#include <array>
#include <string>

#include "envoy/common/union_string.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

// Long enough to force a heap allocation, i.e. longer than the inline capacity.
const std::string& heapString() {
  static const std::string* value = new std::string(512, 'a');
  return *value;
}

const std::string& referenceString() {
  static const std::string* value = new std::string("0123456789012345678901234567890123456789");
  return *value;
}

enum class Flavor { Reference, Inline, Heap };

UnionString makeString(Flavor flavor) {
  UnionString string;
  switch (flavor) {
  case Flavor::Reference:
    string.setReference(referenceString());
    break;
  case Flavor::Inline:
    string.setCopy(absl::string_view("0123456789012345678901234567890123456789"));
    break;
  case Flavor::Heap:
    string.setCopy(heapString());
    break;
  }
  return string;
}

// Measure reading a string out of storage, the hottest operation on the type: every header match,
// comparison, serialization and log line goes through it.
void unionStringGetStringView(benchmark::State& state, Flavor flavor) {
  UnionString string = makeString(flavor);
  for (auto _ : state) { // NOLINT
    benchmark::DoNotOptimize(string.getStringView());
  }
}
BENCHMARK_CAPTURE(unionStringGetStringView, reference, Flavor::Reference);
BENCHMARK_CAPTURE(unionStringGetStringView, inlined, Flavor::Inline);
BENCHMARK_CAPTURE(unionStringGetStringView, heap, Flavor::Heap);

// Measure reading the size alone, which callers use to compute byte sizes and to check emptiness
// without touching the data.
void unionStringSize(benchmark::State& state, Flavor flavor) {
  UnionString string = makeString(flavor);
  for (auto _ : state) { // NOLINT
    benchmark::DoNotOptimize(string.size());
  }
}
BENCHMARK_CAPTURE(unionStringSize, reference, Flavor::Reference);
BENCHMARK_CAPTURE(unionStringSize, inlined, Flavor::Inline);
BENCHMARK_CAPTURE(unionStringSize, heap, Flavor::Heap);

// Measure reading a string out of storage when the storage is not already in cache, which is the
// realistic case for a header map walked once per request.
void unionStringGetStringViewCold(benchmark::State& state, Flavor flavor) {
  // Larger than a core's L2, so that a pass over the array evicts the previous pass.
  constexpr size_t num_strings = 16384;
  std::vector<UnionString> strings;
  strings.reserve(num_strings);
  for (size_t i = 0; i < num_strings; i++) {
    strings.push_back(makeString(flavor));
  }
  size_t index = 0;
  for (auto _ : state) { // NOLINT
    benchmark::DoNotOptimize(strings[index].getStringView());
    index = (index + 1) % num_strings;
  }
}
BENCHMARK_CAPTURE(unionStringGetStringViewCold, reference, Flavor::Reference);
BENCHMARK_CAPTURE(unionStringGetStringViewCold, inlined, Flavor::Inline);
BENCHMARK_CAPTURE(unionStringGetStringViewCold, heap, Flavor::Heap);

// Measure comparing a string against a literal, as header matching does.
void unionStringCompare(benchmark::State& state, Flavor flavor) {
  UnionString string = makeString(flavor);
  for (auto _ : state) { // NOLINT
    benchmark::DoNotOptimize(string == "0123456789012345678901234567890123456789");
  }
}
BENCHMARK_CAPTURE(unionStringCompare, reference, Flavor::Reference);
BENCHMARK_CAPTURE(unionStringCompare, inlined, Flavor::Inline);
BENCHMARK_CAPTURE(unionStringCompare, heap, Flavor::Heap);

// Measure construction and destruction, which happens twice per header added to a map.
void unionStringCreate(benchmark::State& state) {
  for (auto _ : state) { // NOLINT
    UnionString string;
    benchmark::DoNotOptimize(string.size());
  }
}
BENCHMARK(unionStringCreate);

// Measure pointing at data owned elsewhere, the path taken by every inline header key and by
// setReference() values.
void unionStringSetReference(benchmark::State& state) {
  UnionString string;
  for (auto _ : state) { // NOLINT
    string.setReference(referenceString());
    benchmark::DoNotOptimize(string.size());
  }
}
BENCHMARK(unionStringSetReference);

// Measure copying data in, both while it still fits inline and once it needs the heap. The
// arguments are the length of the value copied.
void unionStringSetCopy(benchmark::State& state) {
  const std::string value(state.range(0), 'a');
  UnionString string;
  for (auto _ : state) { // NOLINT
    string.setCopy(value);
    benchmark::DoNotOptimize(string.size());
  }
}
BENCHMARK(unionStringSetCopy)->Arg(8)->Arg(40)->Arg(128)->Arg(512)->Arg(4096);

// Measure overwriting a value that is already on the heap with a longer one, which reallocates.
void unionStringSetCopyGrowing(benchmark::State& state) {
  const std::string small(512, 'a');
  const std::string large(8192, 'b');
  for (auto _ : state) { // NOLINT
    UnionString string;
    string.setCopy(small);
    string.setCopy(large);
    benchmark::DoNotOptimize(string.size());
  }
}
BENCHMARK(unionStringSetCopyGrowing);

// Measure the integer path, used for content-length and other numeric inline headers.
void unionStringSetInteger(benchmark::State& state) {
  UnionString string;
  uint64_t value = 1234567;
  for (auto _ : state) { // NOLINT
    string.setInteger(value++);
    benchmark::DoNotOptimize(string.size());
  }
}
BENCHMARK(unionStringSetInteger);

// Measure appending, as HTTP/1 header field and value parsing does across buffer boundaries.
void unionStringAppend(benchmark::State& state) {
  const std::string chunk(state.range(0), 'a');
  for (auto _ : state) { // NOLINT
    UnionString string;
    for (int i = 0; i < 8; i++) {
      string.append(chunk.data(), chunk.size());
    }
    benchmark::DoNotOptimize(string.size());
  }
}
BENCHMARK(unionStringAppend)->Arg(4)->Arg(64);

// Measure building a string and then moving it, which happens when a value is transferred into a
// header map. UnionString is not move-assignable, so the source is rebuilt every iteration and its
// construction is part of the measurement.
void unionStringCreateAndMove(benchmark::State& state, Flavor flavor) {
  for (auto _ : state) { // NOLINT
    UnionString source = makeString(flavor);
    UnionString moved(std::move(source));
    benchmark::DoNotOptimize(moved.size());
  }
}
BENCHMARK_CAPTURE(unionStringCreateAndMove, reference, Flavor::Reference);
BENCHMARK_CAPTURE(unionStringCreateAndMove, inlined, Flavor::Inline);
BENCHMARK_CAPTURE(unionStringCreateAndMove, heap, Flavor::Heap);

} // namespace
} // namespace Envoy
