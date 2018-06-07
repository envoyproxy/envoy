// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <random>

#include "common/common/assert.h"
#include "common/common/utility.h"

#include "absl/strings/string_view.h"
#include "testing/base/public/benchmark.h"

static const char TextToTrim[] = "\t  the quick brown fox jumps over the lazy dog\n\r\n";
static size_t TextToTrimLength = sizeof(TextToTrim) - 1;

static const char AlreadyTrimmed[] = "the quick brown fox jumps over the lazy dog";
static size_t AlreadyTrimmedLength = sizeof(AlreadyTrimmed) - 1;

static const char CacheControl[] = "private, max-age=300, no-transform";
static size_t CacheControlLength = sizeof(CacheControl) - 1;

// NOLINT(namespace-envoy)

static void BM_AccessLogDateTimeFormatter(benchmark::State& state) {
  int outputBytes = 0;

  // Generate a sequence of times for which the delta between each successive
  // pair of times is uniformly distributed in the range (-10ms, 20ms).
  // This is meant to simulate the situation where requests handled at
  // approximately the same time may get logged out of order.
  static Envoy::SystemTime time(std::chrono::seconds(1522796769));
  static std::mt19937 prng(1); // PRNG with a fixed seed, for repeatability
  static std::uniform_int_distribution<long> distribution(-10, 20);
  for (auto _ : state) {
    // TODO(brian-pane): The next line, which computes the next input timestamp,
    // currently accounts for ~30% of the CPU time of this benchmark test. If
    // the AccessLogDateTimeFormatter implementation is optimized further, we
    // should precompute a sequence of input timestamps so the benchmark's own
    // overhead won't obscure changes in the speed of the code being benchmarked.
    time += std::chrono::milliseconds(static_cast<int>(distribution(prng)));
    outputBytes += Envoy::AccessLogDateTimeFormatter::fromTime(time).length();
  }
  benchmark::DoNotOptimize(outputBytes);
}
BENCHMARK(BM_AccessLogDateTimeFormatter);

// This benchmark is basically similar with the above BM_AccessLogDateTimeFormatter, the only
// difference is the format string input for the Envoy::DateFormatter.
static void BM_DateTimeFormatterWithSubseconds(benchmark::State& state) {
  int outputBytes = 0;

  Envoy::SystemTime time(std::chrono::seconds(1522796769));
  std::mt19937 prng(1);
  std::uniform_int_distribution<long> distribution(-10, 20);
  Envoy::DateFormatter date_formatter("%Y-%m-%dT%H:%M:%s.%3f");
  for (auto _ : state) {
    time += std::chrono::milliseconds(static_cast<int>(distribution(prng)));
    outputBytes += date_formatter.fromTime(time).length();
  }
  benchmark::DoNotOptimize(outputBytes);
}
BENCHMARK(BM_DateTimeFormatterWithSubseconds);

static void BM_DateTimeFormatterWithoutSubseconds(benchmark::State& state) {
  int outputBytes = 0;

  Envoy::SystemTime time(std::chrono::seconds(1522796769));
  std::mt19937 prng(1);
  std::uniform_int_distribution<long> distribution(-10, 20);
  Envoy::DateFormatter date_formatter("%Y-%m-%dT%H:%M:%s");
  for (auto _ : state) {
    time += std::chrono::milliseconds(static_cast<int>(distribution(prng)));
    outputBytes += date_formatter.fromTime(time).length();
  }
  benchmark::DoNotOptimize(outputBytes);
}
BENCHMARK(BM_DateTimeFormatterWithoutSubseconds);

static void BM_RTrimStringView(benchmark::State& state) {
  int accum = 0;
  for (auto _ : state) {
    absl::string_view text(TextToTrim, TextToTrimLength);
    text = Envoy::StringUtil::rtrim(text);
    accum += TextToTrimLength - text.size();
  }
  benchmark::DoNotOptimize(accum);
}
BENCHMARK(BM_RTrimStringView);

static void BM_RTrimStringViewAlreadyTrimmed(benchmark::State& state) {
  int accum = 0;
  for (auto _ : state) {
    absl::string_view text(AlreadyTrimmed, AlreadyTrimmedLength);
    text = Envoy::StringUtil::rtrim(text);
    accum += AlreadyTrimmedLength - text.size();
  }
  benchmark::DoNotOptimize(accum);
}
BENCHMARK(BM_RTrimStringViewAlreadyTrimmed);

static void BM_RTrimStringViewAlreadyTrimmedAndMakeString(benchmark::State& state) {
  int accum = 0;
  for (auto _ : state) {
    absl::string_view text(AlreadyTrimmed, AlreadyTrimmedLength);
    std::string string_copy = std::string(Envoy::StringUtil::rtrim(text));
    accum += AlreadyTrimmedLength - string_copy.size();
  }
  benchmark::DoNotOptimize(accum);
}
BENCHMARK(BM_RTrimStringViewAlreadyTrimmedAndMakeString);

static void BM_FindToken(benchmark::State& state) {
  const absl::string_view cache_control(CacheControl, CacheControlLength);
  for (auto _ : state) {
    RELEASE_ASSERT(Envoy::StringUtil::findToken(cache_control, ",", "no-transform"));
  }
}
BENCHMARK(BM_FindToken);

static bool nextToken(absl::string_view& str, char delim, bool stripWhitespace,
                      absl::string_view* token) {
  while (!str.empty()) {
    absl::string_view::size_type pos = str.find(delim);
    if (pos == absl::string_view::npos) {
      *token = str.substr(0, str.size());
      str.remove_prefix(str.size()); // clears str
    } else {
      *token = str.substr(0, pos);
      str.remove_prefix(pos + 1); // move past token and delim
    }
    if (stripWhitespace) {
      *token = Envoy::StringUtil::trim(*token);
    }
    if (!token->empty()) {
      return true;
    }
  }
  return false;
}

// Experimental alternative implementation of StringUtil::findToken which doesn't create
// a temp vector, but just iterates through the string_view, tokenizing, and matching against
// the token we want. It appears to be about 2.5x to 3x faster on this testcase.
static bool findTokenWithoutSplitting(absl::string_view str, char delim, absl::string_view token,
                                      bool stripWhitespace) {
  for (absl::string_view tok; nextToken(str, delim, stripWhitespace, &tok);) {
    if (tok == token) {
      return true;
    }
  }
  return false;
}

static void BM_FindTokenWithoutSplitting(benchmark::State& state) {
  const absl::string_view cache_control(CacheControl, CacheControlLength);
  for (auto _ : state) {
    RELEASE_ASSERT(findTokenWithoutSplitting(cache_control, ',', "no-transform", true));
  }
}
BENCHMARK(BM_FindTokenWithoutSplitting);

static void BM_FindTokenValueNestedSplit(benchmark::State& state) {
  const absl::string_view cache_control(CacheControl, CacheControlLength);
  absl::string_view max_age;
  for (auto _ : state) {
    for (absl::string_view token : Envoy::StringUtil::splitToken(cache_control, ",")) {
      auto name_value = Envoy::StringUtil::splitToken(token, "=");
      if ((name_value.size() == 2) && (Envoy::StringUtil::trim(name_value[0]) == "max-age")) {
        max_age = Envoy::StringUtil::trim(name_value[1]);
      }
    }
    RELEASE_ASSERT(max_age == "300");
  }
}
BENCHMARK(BM_FindTokenValueNestedSplit);

static void BM_FindTokenValueSearchForEqual(benchmark::State& state) {
  for (auto _ : state) {
    const absl::string_view cache_control(CacheControl, CacheControlLength);
    absl::string_view max_age;
    for (absl::string_view token : Envoy::StringUtil::splitToken(cache_control, ",")) {
      absl::string_view::size_type equals = token.find('=');
      if (equals != absl::string_view::npos &&
          Envoy::StringUtil::trim(token.substr(0, equals)) == "max-age") {
        max_age = Envoy::StringUtil::trim(token.substr(equals + 1));
      }
    }
    RELEASE_ASSERT(max_age == "300");
  }
}
BENCHMARK(BM_FindTokenValueSearchForEqual);

static void BM_FindTokenValueNoSplit(benchmark::State& state) {
  for (auto _ : state) {
    absl::string_view cache_control(CacheControl, CacheControlLength);
    absl::string_view max_age;
    for (absl::string_view token; nextToken(cache_control, ',', true, &token);) {
      absl::string_view name;
      if (nextToken(token, '=', true, &name) && (name == "max-age")) {
        max_age = Envoy::StringUtil::trim(token);
      }
    }
    RELEASE_ASSERT(max_age == "300");
  }
}
BENCHMARK(BM_FindTokenValueNoSplit);

static void BM_IntervalSetInsert17(benchmark::State& state) {
  for (auto _ : state) {
    Envoy::IntervalSetImpl<size_t> interval_set;
    interval_set.insert(7, 10);
    interval_set.insert(-2, -1);
    interval_set.insert(22, 23);
    interval_set.insert(8, 15);
    interval_set.insert(5, 12);
    interval_set.insert(3, 3);
    interval_set.insert(3, 4);
    interval_set.insert(2, 4);
    interval_set.insert(3, 6);
    interval_set.insert(18, 19);
    interval_set.insert(16, 17);
    interval_set.insert(19, 20);
    interval_set.insert(3, 6);
    interval_set.insert(3, 20);
    interval_set.insert(3, 22);
    interval_set.insert(-2, 23);
    interval_set.insert(-3, 24);
  }
}
BENCHMARK(BM_IntervalSetInsert17);

static void BM_IntervalSet4ToVector(benchmark::State& state) {
  Envoy::IntervalSetImpl<size_t> interval_set;
  interval_set.insert(7, 10);
  interval_set.insert(-2, -1);
  interval_set.insert(22, 23);
  interval_set.insert(8, 15);
  for (auto _ : state) {
    benchmark::DoNotOptimize(interval_set.toVector());
  }
}
BENCHMARK(BM_IntervalSet4ToVector);

static void BM_IntervalSet50ToVector(benchmark::State& state) {
  Envoy::IntervalSetImpl<size_t> interval_set;
  for (size_t i = 0; i < 100; i += 2) {
    interval_set.insert(i, i + 1);
  }
  for (auto _ : state) {
    benchmark::DoNotOptimize(interval_set.toVector());
  }
}
BENCHMARK(BM_IntervalSet50ToVector);

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
