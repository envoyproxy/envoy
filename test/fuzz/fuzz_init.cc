#include "test/fuzz/fuzz_init.h"

#include "test/fuzz/fuzz_runner.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Fuzz {
int InitFuzzerForEnvoy(int* argc, char*** argv) {
  // Before parsing gmock flags, set the default value of flag --gmock_verbose to "error".
  // This suppresses logs from NiceMock objects, which can be noisy and provide little value.
  testing::GMOCK_FLAG(verbose) = "error";
  testing::InitGoogleMock(argc, *argv);
  Envoy::Fuzz::Runner::setupEnvironment(1, *argv, spdlog::level::critical);
  return 0;
}
} // namespace Fuzz
} // namespace Envoy
