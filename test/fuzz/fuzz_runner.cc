#include "test/fuzz/fuzz_runner.h"

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/common/utility.h"
#include "common/event/libevent.h"

#include "test/test_common/environment.h"

namespace Envoy {
namespace Fuzz {

spdlog::level::level_enum Runner::log_level_;

PerTestEnvironment::PerTestEnvironment()
    : test_tmpdir_([] {
        const std::string fuzz_path = TestEnvironment::temporaryPath("fuzz_XXXXXX");
        char test_tmpdir[fuzz_path.size() + 1];
        StringUtil::strlcpy(test_tmpdir, fuzz_path.data(), fuzz_path.size() + 1);
        RELEASE_ASSERT(::mkdtemp(test_tmpdir) != nullptr, "");
        return std::string(test_tmpdir);
      }()) {}

void Runner::setupEnvironment(int argc, char** argv, spdlog::level::level_enum default_log_level) {
  Event::Libevent::Global::initialize();

  TestEnvironment::initializeOptions(argc, argv);

  static auto* lock = new Thread::MutexBasicLockable();
  const auto environment_log_level = TestEnvironment::getOptions().logLevel();
  // We only override the default log level if it looks like we're debugging;
  // otherwise the default environment log level might override the default and
  // spew too much when running under a fuzz engine.
  log_level_ =
      environment_log_level <= spdlog::level::debug ? environment_log_level : default_log_level;
  Logger::Registry::initialize(log_level_, TestEnvironment::getOptions().logFormat(), *lock);
}

} // namespace Fuzz
} // namespace Envoy

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** argv) {
  Envoy::Fuzz::Runner::setupEnvironment(1, *argv, spdlog::level::off);
  return 0;
}
