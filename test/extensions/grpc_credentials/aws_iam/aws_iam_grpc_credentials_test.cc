#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/grpc_credential/v3/aws_iam.pb.h"

#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/grpc/google_async_client_impl.h"

#include "extensions/grpc_credentials/well_known_names.h"

#include "test/common/grpc/grpc_client_integration_test_harness.h"
#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Grpc {
namespace {

// AWS IAM credential validation tests.
class GrpcAwsIamClientIntegrationTest : public GrpcSslClientIntegrationTest {
public:
  void SetUp() override {
    GrpcSslClientIntegrationTest::SetUp();
    TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "test_akid", 1);
    TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "test_secret", 1);
  }

  void TearDown() override {
    GrpcSslClientIntegrationTest::TearDown();
    TestEnvironment::unsetEnvVar("AWS_REGION");
    TestEnvironment::unsetEnvVar("AWS_ACCESS_KEY_ID");
    TestEnvironment::unsetEnvVar("AWS_SECRET_ACCESS_KEY");
  }

  void expectExtraHeaders(FakeStream& fake_stream) override {
    AssertionResult result = fake_stream.waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    Http::TestHeaderMapImpl stream_headers(fake_stream.headers());
    const auto auth_header = stream_headers.get_("Authorization");
    const auto auth_parts = StringUtil::splitToken(auth_header, ", ", false);
    ASSERT_EQ(4, auth_parts.size());
    EXPECT_EQ("AWS4-HMAC-SHA256", auth_parts[0]);
    EXPECT_TRUE(absl::StartsWith(auth_parts[1], "Credential=test_akid/"));
    EXPECT_TRUE(absl::EndsWith(auth_parts[1],
                               fmt::format("{}/{}/aws4_request", region_name_, service_name_)));
    EXPECT_EQ("SignedHeaders=host;x-amz-content-sha256;x-amz-date", auth_parts[2]);
    // We don't verify correctness off the signature here, as this is part of the signer unit tests.
    EXPECT_TRUE(absl::StartsWith(auth_parts[3], "Signature="));
  }

  envoy::config::core::v3::GrpcService createGoogleGrpcConfig() override {
    auto config = GrpcSslClientIntegrationTest::createGoogleGrpcConfig();
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->set_credentials_factory_name(credentials_factory_name_);
    auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
    ssl_creds->mutable_root_certs()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));

    std::string config_yaml;
    if (region_in_env_) {
      TestEnvironment::setEnvVar("AWS_REGION", region_name_, 1);
      config_yaml = fmt::format(R"EOF(
"@type": type.googleapis.com/envoy.config.grpc_credential.v2alpha.AwsIamConfig        
service_name: {}
)EOF",
                                service_name_);
    } else {
      config_yaml = fmt::format(R"EOF(
"@type": type.googleapis.com/envoy.config.grpc_credential.v2alpha.AwsIamConfig        
service_name: {}
region: {}
)EOF",
                                service_name_, region_name_);
    }

    auto* plugin_config = google_grpc->add_call_credentials()->mutable_from_plugin();
    plugin_config->set_name(credentials_factory_name_);
    envoy::config::grpc_credential::v3::AwsIamConfig metadata_config;
    Envoy::TestUtility::loadFromYaml(config_yaml, *plugin_config->mutable_typed_config());
    return config;
  }

  bool region_in_env_{};
  std::string service_name_{};
  std::string region_name_{};
  std::string credentials_factory_name_{};
};

INSTANTIATE_TEST_SUITE_P(SslIpVersionsClientType, GrpcAwsIamClientIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(GrpcAwsIamClientIntegrationTest, AwsIamGrpcAuth_ConfigRegion) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  service_name_ = "test_service";
  region_name_ = "test_region_static";
  credentials_factory_name_ = Extensions::GrpcCredentials::GrpcCredentialsNames::get().AwsIam;
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

TEST_P(GrpcAwsIamClientIntegrationTest, AwsIamGrpcAuth_EnvRegion) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  service_name_ = "test_service";
  region_name_ = "test_region_env";
  region_in_env_ = true;
  credentials_factory_name_ = Extensions::GrpcCredentials::GrpcCredentialsNames::get().AwsIam;
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

} // namespace
} // namespace Grpc
} // namespace Envoy
