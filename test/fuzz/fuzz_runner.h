#pragma once

#include <cstdint>
#include <cwchar>

// Bring in DEFINE_PROTO_FUZZER definition as per
// https://github.com/google/libprotobuf-mutator#integrating-with-libfuzzer.
#include "libprotobuf_mutator/src/libfuzzer/libfuzzer_macro.h"
// Bring in FuzzedDataProvider, see
// https://github.com/google/fuzzing/blob/master/docs/split-inputs.md#fuzzed-data-provider
#include "fuzzer/FuzzedDataProvider.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Fuzz {

// Each test may need a sub-environment of that provided by //test/test_common:environment_lib,
// since each fuzz invocation runs in the same process, but might want a distinct tmp sandbox for
// example.
class PerTestEnvironment {
public:
  PerTestEnvironment();
  ~PerTestEnvironment();

  std::string temporaryPath(const std::string& path) const { return test_tmpdir_ + "/" + path; }
  const std::string& testId() const { return test_id_; }

private:
  static uint32_t test_num_;
  const uint32_t per_test_num_;
  const std::string test_tmpdir_;
  const std::string test_id_;
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

/**
 * Establishes a function to run before the test process exits. This enables
 * threads, mocks, and other objects that are expensive to create to be shared
 * between test methods.
 */
void addCleanupHook(std::function<void()>);

/**
 * Runs all cleanup hooks.
 */
void runCleanupHooks();

} // namespace Fuzz
} // namespace Envoy

// Fuzz test startup hook, see
// https://llvm.org/docs/LibFuzzer.html#startup-initialization.
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);

// See https://llvm.org/docs/LibFuzzer.html#fuzz-target.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

#ifdef PERSISTENT_FUZZER
template <typename T> T& initFuzzVar(T* ptr) {
  Envoy::Fuzz::addCleanupHook([ptr]() { delete ptr; });
  return *ptr;
}
#define PERSISTENT_FUZZ_VAR(type, var, args) static type& var = initFuzzVar(new type(args))
#else
#define PERSISTENT_FUZZ_VAR(type, var, args) type var args
#endif

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
