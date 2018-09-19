// This is an Envoy test driver for fuzz tests. Unlike regular Envoy tests, we
// operate in a more restricted environment, comparable to what oss-fuz uses. We
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
#include "common/common/thread.h"
#include "common/filesystem/filesystem_impl.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// List of paths for files in the test corpus.
std::vector<std::string> test_corpus_;

class FuzzerCorpusTest : public ::testing::TestWithParam<std::string> {};

TEST_P(FuzzerCorpusTest, RunOneCorpusFile) {
  ENVOY_LOG_MISC(info, "Corpus file: {}", GetParam());
  const std::string buf = Filesystem::fileReadToEnd(GetParam());
  // Everything from here on is the same as under the fuzzer lib.
  LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(buf.c_str()), buf.size());
}

INSTANTIATE_TEST_CASE_P(CorpusExamples, FuzzerCorpusTest, testing::ValuesIn(test_corpus_));

} // namespace
} // namespace Envoy

int main(int argc, char** argv) {
  // Expected usage: <test path> <corpus path> [other gtest flags]
  RELEASE_ASSERT(argc-- >= 2, "");
  const std::string corpus_path = Envoy::TestEnvironment::getCheckedEnvVar("TEST_SRCDIR") + "/" +
                                  Envoy::TestEnvironment::getCheckedEnvVar("TEST_WORKSPACE") + "/" +
                                  *++argv;
  RELEASE_ASSERT(Envoy::Filesystem::directoryExists(corpus_path), "");
  Envoy::test_corpus_ = Envoy::TestUtility::listFiles(corpus_path, true);
  testing::InitGoogleTest(&argc, argv);
  Envoy::Fuzz::Runner::setupEnvironment(argc, argv, spdlog::level::info);
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logging_context(Envoy::Fuzz::Runner::logLevel(),
                                         Envoy::TestEnvironment::getOptions().logFormat(), lock);

  return RUN_ALL_TESTS();
}
