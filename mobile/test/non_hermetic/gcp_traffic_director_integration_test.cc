// This test is not meant to be run on the command line, because it depends on a GCP
// authentication token provided as a GitHub encrypted secret through a GitHub actions workflow.

#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/clusters/strict_dns/strict_dns_cluster.h"
#include "source/extensions/health_checkers/http/health_checker_impl.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/integration/base_client_integration_test.h"
#include "test/test_common/environment.h"

#include "absl/strings/substitute.h"
#include "absl/synchronization/notification.h"
#include "extension_registry.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/types/c_types.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace Envoy {
namespace {

using ::Envoy::Grpc::SotwOrDelta;
using ::Envoy::Network::Address::IpVersion;

// The One-Platform API endpoint for Traffic Director.
constexpr char TD_API_ENDPOINT[] = "trafficdirectorconsumermesh.googleapis.com";
// The project number of the project, found on the main page of the project in
// Google Cloud Console.
constexpr char PROJECT_ID[] = "33303528656";

// Tests that Envoy Mobile can connect to Traffic Director (an xDS management server offered by GCP)
// via a test GCP project, and can pull down xDS config for the given project.
class GcpTrafficDirectorIntegrationTest
    : public BaseClientIntegrationTest,
      public testing::TestWithParam<std::tuple<IpVersion, SotwOrDelta>> {
public:
  GcpTrafficDirectorIntegrationTest() : BaseClientIntegrationTest(ip_version()) {
    // TODO(https://github.com/envoyproxy/envoy/issues/27848): remove these force registrations
    // once the EngineBuilder APIs support conditional force registration.

    // Register the extensions required for Envoy Mobile.
    ExtensionRegistry::registerFactories();

    // Force register the cluster factories used by the test.
    Upstream::forceRegisterStrictDnsClusterFactory();
    Upstream::forceRegisterHttpHealthCheckerFactory();
    Extensions::LoadBalancingPolices::RoundRobin::forceRegisterFactory();

    std::string root_certs(TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath("test/config/integration/certs/google_root_certs.pem")));

    // API key for the `bct-prod-td-consumer-mesh` GCP test project.
    const char* api_key = std::getenv("GCP_TEST_PROJECT_PROD_API_KEY");
    RELEASE_ASSERT(api_key != nullptr,
                   "GCP_TEST_PROJECT_PROD_API_KEY environment variable not set.");

    Platform::XdsBuilder xds_builder(/*xds_server_address=*/std::string(TD_API_ENDPOINT),
                                     /*xds_server_port=*/443);
    xds_builder.addInitialStreamHeader("x-goog-api-key", std::string(api_key))
        .setSslRootCerts(std::move(root_certs))
        .addClusterDiscoveryService();
    builder_.addLogLevel(Platform::LogLevel::trace)
        .setNodeId(absl::Substitute("projects/$0/networks/default/nodes/111222333444", PROJECT_ID))
        .setXds(std::move(xds_builder));

    // Other test knobs.
    skip_tag_extraction_rule_check_ = true;
    // Envoy Mobile does not use LDS.
    use_lds_ = false;
    // We don't need a fake xDS upstream since we are using Traffic Director.
    create_xds_upstream_ = false;
    sotw_or_delta_ = api_type();

    if (api_type() == SotwOrDelta::UnifiedSotw || api_type() == SotwOrDelta::UnifiedDelta) {
      config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux", "true");
    }
  }

  void TearDown() override { BaseClientIntegrationTest::TearDown(); }

  IpVersion ip_version() const { return std::get<0>(GetParam()); }
  SotwOrDelta api_type() const { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    GrpcOptions, GcpTrafficDirectorIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(SotwOrDelta::Sotw, SotwOrDelta::UnifiedSotw)));

TEST_P(GcpTrafficDirectorIntegrationTest, AdsDynamicClusters) {
  // Starts up Envoy and loads the bootstrap config, which will trigger fetching
  // of the dynamic cluster resources from Traffic Director.
  initialize();

  // Wait for the xDS cluster resources to be retrieved and loaded.
  //
  // There are 5 total active clusters after the Envoy engine has finished initialization.
  //
  // 1. There is one strict dns cluster retrieved from Traffic Director:
  //      backend-svc-do-not-delete
  // 2. There are two static clusters added by the EngineBuilder by default:
  //      base
  //      base_clear
  ASSERT_TRUE(waitForGaugeGe("cluster_manager.active_clusters", 3));

  // TODO(abeyad): Once we have a Envoy Mobile stats API, we can use it to check the
  // actual cluster names.
}

} // namespace
} // namespace Envoy

int main(int argc, char** argv) {
  Envoy::TestEnvironment::initializeOptions(argc, argv);
  std::string error;
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles(
      bazel::tools::cpp::runfiles::Runfiles::Create(argv[0], &error));
  RELEASE_ASSERT(runfiles != nullptr, error);
  Envoy::TestEnvironment::setRunfiles(runfiles.get());

  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logging_context(spdlog::level::level_enum::trace,
                                         Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);
  Envoy::Event::Libevent::Global::initialize();

  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}
