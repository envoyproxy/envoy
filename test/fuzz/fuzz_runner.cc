#include "test/fuzz/fuzz_runner.h"

#include <cstdlib>

#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/event/libevent.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/exe/process_wide.h"

#include "test/test_common/environment.h"

#include "absl/log/globals.h"
#include "gmock/gmock.h"

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
  // We hold on to process_wide to provide RAII cleanup of process-wide
  // state.
  ProcessWide process_wide;
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
      new Logger::Context(log_level_, TestEnvironment::getOptions().logFormat(), *lock, false);
  UNREFERENCED_PARAMETER(logging_context);

  // Suppress all libprotobuf non-fatal logging as long as this object exists.
  // For fuzzing, this prevents logging when parsing text-format protos fails,
  // deprecated fields are used, etc.
  if (log_level_ > spdlog::level::debug) {
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfinity);
  }
}

using Hooks = std::vector<std::function<void()>>;
static Hooks* cleanup_hooks = nullptr;

void addCleanupHook(std::function<void()> cleanup) {
  if (cleanup_hooks == nullptr) {
    cleanup_hooks = new Hooks;
  }
  cleanup_hooks->push_back(cleanup);
}

void runCleanupHooks() {
  if (cleanup_hooks != nullptr) {
    // Run hooks in reverse order from how they were added.
    for (auto iter = cleanup_hooks->rbegin(), end = cleanup_hooks->rend(); iter != end; ++iter) {
      (*iter)();
    }
    delete cleanup_hooks;
    cleanup_hooks = nullptr;
  }
}

} // namespace Fuzz
} // namespace Envoy

// LLVMFuzzerInitialize() is called by LibFuzzer once before fuzzing starts.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  // Before parsing gmock flags, set the default value of flag --gmock_verbose to "error".
  // This suppresses logs from NiceMock objects, which can be noisy and provide little value.
  testing::GMOCK_FLAG(verbose) = "error";
  testing::InitGoogleMock(argc, *argv);
  Envoy::Fuzz::Runner::setupEnvironment(1, *argv, spdlog::level::critical);
  atexit(Envoy::Fuzz::runCleanupHooks);
  return 0;
}
