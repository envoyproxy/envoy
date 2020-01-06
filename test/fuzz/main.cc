// This is an Envoy test driver for fuzz tests. Unlike regular Envoy tests, we
// operate in a more restricted environment, comparable to what oss-fuzz uses. We
// use the same Envoy::Fuzz::Runner library that oss-fuzz
// (https://github.com/google/oss-fuzz) links against, providing the ability to
// develop tests purely inside the Envoy repository and also to regression test
// fuzz tests
// (https://github.com/google/oss-fuzz/blob/master/docs/ideal_integration.md#regression-testing).
//
// Below, we use a similar approach to
// https://github.com/grpc/grpc/blob/master/test/core/util/fuzzer_corpus_test.cc,
// where gtest parameterized tests are used to iterate over the corpus. This is
// neat, as we get features likes --gtest_filter to select over the corpus
// and the reporting features of gtest.

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/debugging/symbolize.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "common/signal/signal_action.h"
#endif

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// List of paths for files in the test corpus.
std::vector<std::string> test_corpus_;

class FuzzerCorpusTest : public testing::TestWithParam<std::string> {
protected:
  FuzzerCorpusTest() : api_(Api::createApiForTest()) {}

  Api::ApiPtr api_;
};

TEST_P(FuzzerCorpusTest, RunOneCorpusFile) {
  ENVOY_LOG_MISC(info, "Corpus file: {}", GetParam());
  const std::string buf = api_->fileSystem().fileReadToEnd(GetParam());
  // Everything from here on is the same as under the fuzzer lib.
  LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(buf.c_str()), buf.size());
}

INSTANTIATE_TEST_SUITE_P(CorpusExamples, FuzzerCorpusTest, testing::ValuesIn(test_corpus_));

} // namespace
} // namespace Envoy

int main(int argc, char** argv) {
#ifndef __APPLE__
  absl::InitializeSymbolizer(argv[0]);
#endif
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif
  // Expected usage: <test path> <corpus paths..> [other gtest flags]
  RELEASE_ASSERT(argc >= 2, "");
  // Consider any file after the test path which doesn't have a - prefix to be a corpus entry.
  uint32_t input_args = 0;
  // Ensure we cleanup API resources before we jump into the tests, the test API creates a singleton
  // time system that we don't want to leak into gtest.
  {
    Envoy::Api::ApiPtr api = Envoy::Api::createApiForTest();
    for (int i = 1; i < argc; ++i) {
      const std::string arg{argv[i]};
      if (arg.empty() || arg[0] == '-') {
        break;
      }
      ++input_args;
      // Outputs from envoy_directory_genrule might be directories or we might
      // have artisanal files.
      if (api->fileSystem().directoryExists(arg)) {
        const auto paths = Envoy::TestUtility::listFiles(arg, true);
        Envoy::test_corpus_.insert(Envoy::test_corpus_.begin(), paths.begin(), paths.end());
      } else {
        Envoy::test_corpus_.emplace_back(arg);
      }
    }
  }
  argc -= input_args;
  for (ssize_t i = 1; i < argc; ++i) {
    argv[i] = argv[i + input_args];
  }

  testing::InitGoogleTest(&argc, argv);
  Envoy::Fuzz::Runner::setupEnvironment(argc, argv, spdlog::level::info);

  return RUN_ALL_TESTS();
}
