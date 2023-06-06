#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/grpc/google_grpc_creds_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/clusters/eds/eds.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/grpc_subscription_factory.h"
#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/integration/base_client_integration_test.h"
#include "test/test_common/environment.h"

#include "absl/strings/substitute.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/engine_handle.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace {

using ::Envoy::Grpc::SotwOrDelta;
using ::Envoy::Network::Address::IpVersion;

// The One-Platform API endpoint for Traffic Director.
constexpr char TD_API_ENDPOINT[] = "trafficdirector.googleapis.com";
// The name of the project in Google Cloud Console; copied from the project_id
// field in the generated JWT token.
constexpr char PROJECT_NAME[] = "td-testing-gfq";
// The project number of the project, found on the main page of the project in
// Google Cloud Console.
constexpr char PROJECT_ID[] = "798832730858";
// Copied from the "client_id" field in the generated JWT token.
constexpr char CLIENT_ID[] = "102524055118681734203";
// Copied from the "private_key_id" field in the generated JWT token.
constexpr char PRIVATE_KEY_ID[] = "e07f02d49044a533cf4342d138eacecc6acdb6ed";

// Using a JWT token to authenticate to Traffic Director.
std::string jwtToken() {
  const std::string email =
      absl::Substitute("$0-compute@developer.gserviceaccount.com", PROJECT_ID);
  const std::string cert_url = absl::Substitute("https://www.googleapis.com/robot/v1/metadata/x509/"
                                                "$0-compute%40developer.gserviceaccount.com",
                                                PROJECT_ID);
  const std::string private_key(TestEnvironment::getCheckedEnvVar("GCP_JWT_PRIVATE_KEY"));

  return absl::Substitute(
      R"json({
        "private_key": "$0",
        "private_key_id": "$1",
        "project_id": "$2",
        "client_email": "$3",
        "client_id": "$4",
        "client_x509_cert_url": "$5",
        "type": "service_account",
        "auth_uri": "https://accounts.google.com/o/oauth2/auth",
        "token_uri": "https://oauth2.googleapis.com/token",
        "auth_provider_x509_cert_url": "https://www.googleapis.com/oauth2/v1/certs"
      })json",
      private_key, PRIVATE_KEY_ID, PROJECT_NAME, email, CLIENT_ID, cert_url);
}

// Tests that Envoy Mobile can connect to Traffic Director (an xDS management server offered by GCP)
// via a test GCP project, and can pull down xDS config for the given project.
class GcpTrafficDirectorIntegrationTest
    : public Envoy::BaseClientIntegrationTest,
      public testing::TestWithParam<std::tuple<IpVersion, SotwOrDelta>> {
public:
  GcpTrafficDirectorIntegrationTest() : BaseClientIntegrationTest(ip_version()) {
    // Force register the Google gRPC library.
    Grpc::forceRegisterDefaultGoogleGrpcCredentialsFactory();
    // Force register the gRPC mux implementations.
    Config::forceRegisterGrpcMuxFactory();
    Config::forceRegisterNewGrpcMuxFactory();
    Config::forceRegisterAdsConfigSubscriptionFactory();
    // Force register the cluster factories used by the test.
    Upstream::forceRegisterEdsClusterFactory();

    std::string root_certs(TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath("test/config/integration/certs/google_root_certs.pem")));

    // TODO(abeyad): switch to using API key authentication instead of a JWT token.
    builder_
        .setNodeId(absl::Substitute("projects/$0/networks/default/nodes/111222333444", PROJECT_ID))
        .addCdsLayer()
        .setAggregatedDiscoveryService(std::string(TD_API_ENDPOINT), /*port=*/443, jwtToken(),
                                       Envoy::Platform::DefaultJwtTokenLifetimeSeconds,
                                       std::move(root_certs));

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
    testing::Combine(testing::ValuesIn(Envoy::TestEnvironment::getIpVersionsForTest()),
                     testing::Values(SotwOrDelta::Sotw, SotwOrDelta::UnifiedSotw)));

TEST_P(GcpTrafficDirectorIntegrationTest, AdsDynamicClusters) {
  // Starts up Envoy and loads the bootstrap config, which will trigger fetching
  // of the dynamic cluster resources from Traffic Director.
  initialize();

  // Wait for the xDS cluster resources to be retrieved and loaded.
  ASSERT_TRUE(waitForGaugeGe("cluster_manager.active_clusters", 3));
}

} // namespace
} // namespace Envoy
