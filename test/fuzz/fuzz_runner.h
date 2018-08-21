#pragma once

#include <cstdint>
#include <cwchar>

// Bring in DEFINE_PROTO_FUZZER definition as per
// https://github.com/google/libprotobuf-mutator#integrating-with-libfuzzer.
#include "libprotobuf_mutator/src/libfuzzer/libfuzzer_macro.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Fuzz {

// Each test may need a sub-environment of that provided by //test/test_common:environment_lib,
// since each fuzz invocation runs in the same process, but might want a distinct tmp sandbox for
// example.
class PerTestEnvironment {
public:
  PerTestEnvironment();

  std::string temporaryPath(const std::string& path) const { return test_tmpdir_ + "/" + path; }

private:
  const std::string test_tmpdir_;
};

class Runner {
public:
  /**
   * Setup the environment for fuzz testing. Multiple execute() runs may be
   * invoked in this environment.
   * @param argc number of command-line args.
   * @param argv array of command-line args.
   * @param default_loglevel default log level (overridable with -l).
   */
  static void setupEnvironment(int argc, char** argv, spdlog::level::level_enum default_log_level);

  /**
   * @return spdlog::level::level_enum the log level for the fuzzer.
   */
  static spdlog::level::level_enum logLevel() { return log_level_; }

private:
  static spdlog::level::level_enum log_level_;
};

} // namespace Fuzz
} // namespace Envoy

// Fuzz test startup hook, see
// https://llvm.org/docs/LibFuzzer.html#startup-initialization.
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);

// See https://llvm.org/docs/LibFuzzer.html#fuzz-target.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

#define DEFINE_TEST_ONE_INPUT_IMPL                                                                 \
  extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {                        \
    EnvoyTestOneInput(data, size);                                                                 \
    return 0;                                                                                      \
  }

/**
 * Define a fuzz test. This should be used to define a fuzz_cc_fuzz_test_target with:
 *
 * DEFINE_FUZZER(const uint8_t* buf, size_t len) {
 *   // Do some test stuff with buf/len.
 *   return 0;
 * }
 */
#define DEFINE_FUZZER                                                                              \
  static void EnvoyTestOneInput(const uint8_t* buf, size_t len);                                   \
  DEFINE_TEST_ONE_INPUT_IMPL                                                                       \
  static void EnvoyTestOneInput
