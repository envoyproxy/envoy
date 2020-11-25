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

} // namespace Fuzz
} // namespace Envoy
