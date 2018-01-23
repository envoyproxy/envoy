// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

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

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
