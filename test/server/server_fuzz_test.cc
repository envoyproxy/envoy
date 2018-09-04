#include <fstream>

#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/proto_descriptors.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"

namespace Envoy {
namespace Server {

void makePortHermetic(envoy::api::v2::core::Address& address) {
  if (address.has_socket_address()) {
    address.mutable_socket_address()->set_port_value(0);
  }
}

envoy::config::bootstrap::v2::Bootstrap
makeHermeticPathsAndPorts(Fuzz::PerTestEnvironment& test_env,
                          const envoy::config::bootstrap::v2::Bootstrap& input) {
  envoy::config::bootstrap::v2::Bootstrap output(input);
  // This is not a complete list of places where we need to zero out ports or sanitize paths, so we
  // should adapt it as we go and encounter places that we need to stabilize server test flakes.
  // config_validation_fuzz_test doesn't need to do this sanitization, so should pickup the coverage
  // we lose here. If we don't sanitize here, we get flakes due to port bind conflicts, file
  // conflicts, etc.
  output.mutable_admin()->set_access_log_path(test_env.temporaryPath("admin.log"));
  if (output.admin().has_address()) {
    makePortHermetic(*output.mutable_admin()->mutable_address());
  }
  if (output.has_runtime()) {
    output.mutable_runtime()->set_symlink_root(test_env.temporaryPath(""));
  }
  for (auto& listener : *output.mutable_static_resources()->mutable_listeners()) {
    if (listener.has_address()) {
      makePortHermetic(*listener.mutable_address());
    }
  }
  for (auto& cluster : *output.mutable_static_resources()->mutable_clusters()) {
    for (auto& host : *cluster.mutable_hosts()) {
      makePortHermetic(host);
    }
  }
  return output;
}

DEFINE_PROTO_FUZZER(const envoy::config::bootstrap::v2::Bootstrap& input) {
  testing::NiceMock<MockOptions> options;
  DefaultTestHooks hooks;
  testing::NiceMock<MockHotRestart> restart;
  Stats::TestIsolatedStoreImpl stats_store;
  Thread::MutexBasicLockable fakelock;
  TestComponentFactory component_factory;
  ThreadLocal::InstanceImpl thread_local_instance;
  DangerousDeprecatedTestTime test_time;
  Fuzz::PerTestEnvironment test_env;

  RELEASE_ASSERT(Envoy::Server::validateProtoDescriptors(), "");

  {
    const std::string bootstrap_path = test_env.temporaryPath("bootstrap.pb_text");
    std::ofstream bootstrap_file(bootstrap_path);
    bootstrap_file << makeHermeticPathsAndPorts(test_env, input).DebugString();
    options.config_path_ = bootstrap_path;
    options.v2_config_only_ = true;
    options.log_level_ = Fuzz::Runner::logLevel();
  }

  std::unique_ptr<InstanceImpl> server;
  try {
    server = std::make_unique<InstanceImpl>(
        options, test_time.timeSource(),
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"), hooks, restart, stats_store,
        fakelock, component_factory, std::make_unique<Runtime::RandomGeneratorImpl>(),
        thread_local_instance);
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Controlled EnvoyException exit: {}", ex.what());
    return;
  }
  // If we were successful, run any pending events on the main thread's dispatcher loop. These might
  // be, for example, pending DNS resolution callbacks. If they generate exceptions, we want to
  // explode and fail the test, hence we do this outside of the try-catch above.
  server->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
}

} // namespace Server
} // namespace Envoy
