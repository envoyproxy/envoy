#include <fstream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "common/common/thread.h"
#include "common/network/address_impl.h"

#include "server/config_validation/server.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Server {
namespace {

// Derived from //test/server:server_fuzz_test.cc, but starts the server in configuration validation
// mode (quits upon validation of the given config)
DEFINE_PROTO_FUZZER(const envoy::config::bootstrap::v3::Bootstrap& input) {
  testing::NiceMock<MockOptions> options;
  TestComponentFactory component_factory;
  Fuzz::PerTestEnvironment test_env;

  const std::string bootstrap_path = test_env.temporaryPath("bootstrap.pb_text");
  std::ofstream bootstrap_file(bootstrap_path);
  bootstrap_file << input.DebugString();
  options.config_path_ = bootstrap_path;
  options.log_level_ = Fuzz::Runner::logLevel();

  try {
    validateConfig(options, Network::Address::InstanceConstSharedPtr(), component_factory,
                   Thread::threadFactoryForTest(), Filesystem::fileSystemForTest());
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Controlled EnvoyException exit: {}", ex.what());
  }
}

} // namespace
} // namespace Server
} // namespace Envoy
