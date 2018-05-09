#include "test/fuzz/fuzz_runner.h"

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"

#include "test/test_common/environment.h"

namespace Envoy {
namespace Fuzz {

void Runner::setupEnvironment(int argc, char** argv) {
  Event::Libevent::Global::initialize();

  TestEnvironment::initializeOptions(argc, argv);

  static auto* lock = new Thread::MutexBasicLockable();
  const auto environment_log_level = TestEnvironment::getOptions().logLevel();
  Logger::Registry::initialize(std::min(environment_log_level, spdlog::level::info),
                               TestEnvironment::getOptions().logFormat(), *lock);
}

} // namespace Fuzz
} // namespace Envoy

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** argv) {
  Envoy::Fuzz::Runner::setupEnvironment(1, *argv);
  return 0;
}
