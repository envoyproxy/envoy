#include <fstream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/common/thread.h"
#include "source/common/network/address_impl.h"
#include "source/server/config_validation/server.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/server.h"
#include "test/mocks/server/options.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Server {
namespace {

// Derived from //test/server:server_fuzz_test.cc, but starts the server in configuration validation
// mode (quits upon validation of the given config)
DEFINE_PROTO_FUZZER(const envoy::config::bootstrap::v3::Bootstrap& input) {
  envoy::config::bootstrap::v3::Bootstrap sanitizedInput(input);
  for (::envoy::config::cluster::v3::Cluster& cluster :
       *sanitizedInput.mutable_static_resources()->mutable_clusters()) {
    if (cluster.load_assignment().endpoints_size() > 10) {
      ENVOY_LOG_MISC(debug,
                     "Rejecting input with more the 10 endpoints to keep runtime acceptable. "
                     "Current #endpoints: {}",
                     cluster.load_assignment().endpoints_size());
      return;
    }
    // TODO(asraa): QUIC is not enabled in production code yet, so remove references for HTTP3.
    // Tracked at https://github.com/envoyproxy/envoy/issues/9513.
    for (auto& health_check : *cluster.mutable_health_checks()) {
      if (health_check.http_health_check().codec_client_type() ==
          envoy::type::v3::CodecClientType::HTTP3) {
        health_check.mutable_http_health_check()->clear_codec_client_type();
      }
    }
  }
  testing::NiceMock<MockOptions> options;
  TestComponentFactory component_factory;
  ABSL_ATTRIBUTE_UNUSED Filesystem::ScopedUseMemfiles use_memfiles{true};

  const std::string bootstrap_path =
      TestEnvironment::writeStringToFileForTest("bootstrap.pb_text", sanitizedInput.DebugString());
  options.config_path_ = bootstrap_path;
  options.log_level_ = Fuzz::Runner::logLevel();

  try {
    validateConfig(options, std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"),
                   component_factory, Thread::threadFactoryForTest(),
                   Filesystem::fileSystemForTest());
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Controlled EnvoyException exit: {}", ex.what());
  }
}

} // namespace
} // namespace Server
} // namespace Envoy
