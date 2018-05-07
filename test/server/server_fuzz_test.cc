#include <fstream>

#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

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
  DefaultTestHooks hooks;
  testing::NiceMock<MockHotRestart> restart;
  Stats::TestIsolatedStoreImpl stats_store;
  Thread::MutexBasicLockable fakelock;
  TestComponentFactory component_factory;
  ThreadLocal::InstanceImpl thread_local_instance;

  {
    const std::string bootstrap_path = TestEnvironment::temporaryPath("bootstrap.pb_text");
    std::ofstream bootstrap_file(bootstrap_path);
    bootstrap_file << input.DebugString();
    options.config_path_ = bootstrap_path;
    options.v2_config_only_ = true;
  }

  try {
    auto server = std::make_unique<InstanceImpl>(
        options, std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"), hooks, restart,
        stats_store, fakelock, component_factory, std::make_unique<Runtime::RandomGeneratorImpl>(),
        thread_local_instance);
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Controlled EnvoyException exit: {}", ex.what());
  }
}

} // namespace Server
} // namespace Envoy
