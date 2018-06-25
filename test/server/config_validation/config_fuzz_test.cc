#include <fstream>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.validate.h"

#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/hot_restart_nop_impl.h"
#include "server/options_impl.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Server {

DEFINE_PROTO_FUZZER(const envoy::config::bootstrap::v2::Bootstrap& input) {
  testing::NiceMock<MockOptions> options;
  TestComponentFactory component_factory;
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;

  const std::string bootstrap_path = TestEnvironment::temporaryPath("bootstrap.pb_text");
  std::ofstream bootstrap_file(bootstrap_path);
  bootstrap_file << input.DebugString();
  options.config_path_ = bootstrap_path;
  options.v2_config_only_ = true;
  try {
    ValidationInstance server(options,
                              std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"),
                              stats_store, access_log_lock, component_factory);
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Controlled EnvoyException exit: {}", ex.what());
  }
}

} // namespace Server
} // namespace Envoy
