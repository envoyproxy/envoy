#include <fstream>

#include "common/network/address_impl.h"

#include "server/config_validation/server.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Server {
// Derived from //test/server:server_fuzz_test.cc, but starts the server in configuration validation
// mode (quits upon validation of the given config)
DEFINE_PROTO_FUZZER(const envoy::config::bootstrap::v2::Bootstrap& input) {
  testing::NiceMock<MockOptions> options;
  TestComponentFactory component_factory;
  Fuzz::PerTestEnvironment test_env;

  const std::string bootstrap_path = test_env.temporaryPath("bootstrap.pb_text");
  std::ofstream bootstrap_file(bootstrap_path);
  bootstrap_file << input.DebugString();
  options.config_path_ = bootstrap_path;
  options.v2_config_only_ = true;
  options.log_level_ = Fuzz::Runner::logLevel();

  try {
    validateConfig(options, Network::Address::InstanceConstSharedPtr(), component_factory);
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Controlled EnvoyException exit: {}", ex.what());
  }
}

} // namespace Server
} // namespace Envoy
