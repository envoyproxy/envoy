#pragma once

#include <cstdint>
#include <cwchar>

namespace Envoy {
namespace Fuzz {

class Runner {
public:
  /**
   * Setup the environment for fuzz testing. Multiple execute() runs may be
   * invoked in this environment.
   * @param argc number of command-line args.
   * @param argv array of command-line args.
   */
  static void setupEnvironment(int argc, char** argv);

  /**
   * Execute a single fuzz test. This should be implemented by the
   * envoy_cc_fuzz_test target. See
   * https://llvm.org/docs/LibFuzzer.html#fuzz-target.
   */
  static void execute(const uint8_t* data, size_t size);
};

} // namespace Fuzz
} // namespace Envoy

// Fuzz test startup hook, see
// https://llvm.org/docs/LibFuzzer.html#startup-initialization.
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);

// Entry point from fuzzing library driver, see
// https://llvm.org/docs/LibFuzzer.html#fuzz-target.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
