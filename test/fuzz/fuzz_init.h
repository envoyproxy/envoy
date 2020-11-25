#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {
int InitFuzzerForEnvoy(int* argc, char*** argv);
} // namespace Fuzz
} // namespace Envoy

// Fuzz test startup hook, see
// https://llvm.org/docs/LibFuzzer.html#startup-initialization.
// LLVMFuzzerInitialize() is called by LibFuzzer once before fuzzing starts.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  // Before parsing gmock flags, set the default value of flag --gmock_verbose to "error".
  // This suppresses logs from NiceMock objects, which can be noisy and provide little value.
  return Envoy::Fuzz::InitFuzzerForEnvoy(argc, argv);
}

// See https://llvm.org/docs/LibFuzzer.html#fuzz-target.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

#ifdef PERSISTENT_FUZZER
#define PERSISTENT_FUZZ_VAR static
#else
#define PERSISTENT_FUZZ_VAR
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
