#include <fstream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/address.pb.h"

#include "common/common/random_generator.h"
#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/listener_hooks.h"
#include "server/server.h"

#include "test/common/runtime/utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/integration/server.h"
#include "test/mocks/server/hot_restart.h"
#include "test/mocks/server/options.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"

namespace Envoy {
namespace Server {
namespace {

void makePortHermetic(Fuzz::PerTestEnvironment& test_env,
                      envoy::config::core::v3::Address& address) {
  if (address.has_socket_address()) {
    address.mutable_socket_address()->set_port_value(0);
  } else if (address.has_pipe() || address.has_envoy_internal_address()) {
    // TODO(asraa): Remove this work-around to replace EnvoyInternalAddress when implemented and
    // remove condition at line 74.
    address.mutable_pipe()->set_path("@" + test_env.testId() + address.pipe().path());
  }
}

envoy::config::bootstrap::v3::Bootstrap
makeHermeticPathsAndPorts(Fuzz::PerTestEnvironment& test_env,
                          const envoy::config::bootstrap::v3::Bootstrap& input) {
  envoy::config::bootstrap::v3::Bootstrap output(input);
  // This is not a complete list of places where we need to zero out ports or sanitize paths, so we
  // should adapt it as we go and encounter places that we need to stabilize server test flakes.
  // config_validation_fuzz_test doesn't need to do this sanitization, so should pickup the coverage
  // we lose here. If we don't sanitize here, we get flakes due to port bind conflicts, file
  // conflicts, etc.
  output.clear_admin();
  // The header_prefix is a write-once then read-only singleton that persists across tests. We clear
  // this field so that fuzz tests don't fail over multiple iterations.
  output.clear_header_prefix();
  if (output.has_hidden_envoy_deprecated_runtime()) {
    output.mutable_hidden_envoy_deprecated_runtime()->set_symlink_root(test_env.temporaryPath(""));
  }
  for (auto& listener : *output.mutable_static_resources()->mutable_listeners()) {
    if (listener.has_address()) {
      makePortHermetic(test_env, *listener.mutable_address());
    }
  }
  for (auto& cluster : *output.mutable_static_resources()->mutable_clusters()) {
    for (auto& health_check : *cluster.mutable_health_checks()) {
      // TODO(asraa): QUIC is not enabled in production code yet, so remove references for HTTP3.
      // Tracked at https://github.com/envoyproxy/envoy/issues/9513.
      if (health_check.http_health_check().codec_client_type() ==
          envoy::type::v3::CodecClientType::HTTP3) {
        health_check.mutable_http_health_check()->clear_codec_client_type();
      }
    }
    // We may have both deprecated hosts() or load_assignment().
    for (auto& host : *cluster.mutable_hidden_envoy_deprecated_hosts()) {
      makePortHermetic(test_env, host);
    }
    for (int j = 0; j < cluster.load_assignment().endpoints_size(); ++j) {
      auto* locality_lb = cluster.mutable_load_assignment()->mutable_endpoints(j);
      for (int k = 0; k < locality_lb->lb_endpoints_size(); ++k) {
        auto* lb_endpoint = locality_lb->mutable_lb_endpoints(k);
        if (lb_endpoint->endpoint().address().has_socket_address() ||
            lb_endpoint->endpoint().address().has_envoy_internal_address()) {
          makePortHermetic(test_env, *lb_endpoint->mutable_endpoint()->mutable_address());
        }
      }
    }
  }
  return output;
}

DEFINE_PROTO_FUZZER(const envoy::config::bootstrap::v3::Bootstrap& input) {
  testing::NiceMock<MockOptions> options;
  DefaultListenerHooks hooks;
  testing::NiceMock<MockHotRestart> restart;
  Stats::TestIsolatedStoreImpl stats_store;
  Thread::MutexBasicLockable fakelock;
  TestComponentFactory component_factory;
  ThreadLocal::InstanceImpl thread_local_instance;
  DangerousDeprecatedTestTime test_time;
  Fuzz::PerTestEnvironment test_env;
  Init::ManagerImpl init_manager{"Server"};

  {
    const std::string bootstrap_path = test_env.temporaryPath("bootstrap.pb_text");
    std::ofstream bootstrap_file(bootstrap_path);
    bootstrap_file << makeHermeticPathsAndPorts(test_env, input).DebugString();
    options.config_path_ = bootstrap_path;
    options.log_level_ = Fuzz::Runner::logLevel();
  }

  std::unique_ptr<InstanceImpl> server;
  try {
    server = std::make_unique<InstanceImpl>(
        init_manager, options, test_time.timeSystem(),
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"), hooks, restart, stats_store,
        fakelock, component_factory, std::make_unique<Random::RandomGeneratorImpl>(),
        thread_local_instance, Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
        nullptr);
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Controlled EnvoyException exit: {}", ex.what());
    return;
  }
  // If we were successful, run any pending events on the main thread's dispatcher loop. These might
  // be, for example, pending DNS resolution callbacks. If they generate exceptions, we want to
  // explode and fail the test, hence we do this outside of the try-catch above.
  server->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
}

} // namespace
} // namespace Server
} // namespace Envoy
