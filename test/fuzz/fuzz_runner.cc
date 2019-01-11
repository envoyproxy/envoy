#include "test/fuzz/fuzz_runner.h"

#include "common/common/thread.h"
#include "common/common/utility.h"
#include "common/event/libevent.h"

#include "test/test_common/environment.h"

namespace Envoy {
namespace Fuzz {

spdlog::level::level_enum Runner::log_level_;

uint32_t PerTestEnvironment::test_num_;

PerTestEnvironment::PerTestEnvironment()
    : per_test_num_(test_num_++),
      test_tmpdir_(TestEnvironment::temporaryPath(fmt::format("fuzz_{}", per_test_num_))),
      test_id_(std::to_string(HashUtil::xxHash64(test_tmpdir_))) {
  TestEnvironment::createPath(test_tmpdir_);
}

PerTestEnvironment::~PerTestEnvironment() { TestEnvironment::removePath(test_tmpdir_); }

void Runner::setupEnvironment(int argc, char** argv, spdlog::level::level_enum default_log_level) {
  Event::Libevent::Global::initialize();

  TestEnvironment::initializeOptions(argc, argv);

  const auto environment_log_level = TestEnvironment::getOptions().logLevel();
  // We only override the default log level if it looks like we're debugging;
  // otherwise the default environment log level might override the default and
  // spew too much when running under a fuzz engine.
  log_level_ =
      environment_log_level <= spdlog::level::debug ? environment_log_level : default_log_level;
  // This needs to work in both the Envoy test shim and oss-fuzz build environments, so we can't
  // allocate in main.cc. Instead, just create these non-PODs to live forever, since we don't get a
  // shutdown hook (see
  // https://github.com/llvm-mirror/compiler-rt/blob/master/lib/fuzzer/FuzzerInterface.h).
  static auto* lock = new Thread::MutexBasicLockable();
  static auto* logging_context =
      new Logger::Context(log_level_, TestEnvironment::getOptions().logFormat(), *lock);
  UNREFERENCED_PARAMETER(logging_context);
}

} // namespace Fuzz
} // namespace Envoy

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** argv) {
  Envoy::Fuzz::Runner::setupEnvironment(1, *argv, spdlog::level::critical);
  return 0;
}
