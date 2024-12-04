#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/grpc_credential/v3/aws_iam.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/google_async_client_impl.h"
#include "source/extensions/common/aws/signer.h"
#include "source/extensions/grpc_credentials/aws_iam/config.h"

#include "test/common/grpc/grpc_client_integration_test_harness.h"
#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

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
    if (call_credentials_ != CallCredentials::FromPlugin) {
      return;
    }
    AssertionResult result = fake_stream.waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    Http::TestRequestHeaderMapImpl stream_headers(fake_stream.headers());
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

    switch (call_credentials_) {
    case CallCredentials::FromPlugin: {
      std::string config_yaml;
      switch (region_location_) {
      case RegionLocation::InEnvironment:
        TestEnvironment::setEnvVar("AWS_REGION", region_name_, 1);
        ABSL_FALLTHROUGH_INTENDED;
      case RegionLocation::NotProvided:
        config_yaml = fmt::format(R"EOF(
  "@type": type.googleapis.com/envoy.config.grpc_credential.v3.AwsIamConfig
  service_name: {}
  )EOF",
                                  service_name_);
        break;
      case RegionLocation::InConfig:
        config_yaml = fmt::format(R"EOF(
  "@type": type.googleapis.com/envoy.config.grpc_credential.v3.AwsIamConfig
  service_name: {}
  region: {}
  )EOF",
                                  service_name_, region_name_);
        break;
      }

      auto* plugin_config = google_grpc->add_call_credentials()->mutable_from_plugin();
      plugin_config->set_name(credentials_factory_name_);
      Envoy::TestUtility::loadFromYaml(config_yaml, *plugin_config->mutable_typed_config());
      return config;
    }
    case CallCredentials::AccessToken:
      google_grpc->add_call_credentials()->mutable_access_token()->assign("foo");
      return config;
    default:
      return config;
    }
  }
  enum class RegionLocation {
    NotProvided,
    InEnvironment,
    InConfig,
  };

  enum class CallCredentials {
    FromPlugin,
    AccessToken,
  };

  RegionLocation region_location_ = RegionLocation::NotProvided;
  CallCredentials call_credentials_ = CallCredentials::FromPlugin;
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
  region_location_ = RegionLocation::InConfig;
  credentials_factory_name_ = "envoy.grpc_credentials.aws_iam";
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

TEST_P(GrpcAwsIamClientIntegrationTest, AwsIamGrpcAuth_EnvRegion) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  service_name_ = "test_service";
  region_name_ = "test_region_env";
  region_location_ = RegionLocation::InEnvironment;
  credentials_factory_name_ = "envoy.grpc_credentials.aws_iam";
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

TEST_P(GrpcAwsIamClientIntegrationTest, AwsIamGrpcAuth_NoRegion) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  service_name_ = "test_service";
  region_name_ = "test_region_env";
  region_location_ = RegionLocation::NotProvided;
  credentials_factory_name_ = "envoy.grpc_credentials.aws_iam";
  EXPECT_THROW_WITH_REGEX(initialize();, EnvoyException, "Region string");
}

TEST_P(GrpcAwsIamClientIntegrationTest, AwsIamGrpcAuth_UnexpectedCallCredentials) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  call_credentials_ = CallCredentials::AccessToken;
  credentials_factory_name_ = "envoy.grpc_credentials.aws_iam";
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

class MockSigner : public Envoy::Extensions::Common::Aws::Signer {
public:
  ~MockSigner() override = default;
  absl::Status sign(Http::RequestMessage&, bool, absl::string_view) override {
    return absl::NotFoundError("test");
  };
  absl::Status signEmptyPayload(Http::RequestHeaderMap&, const absl::string_view = "") override {
    return absl::OkStatus();
  };
  absl::Status signUnsignedPayload(Http::RequestHeaderMap&, const absl::string_view = "") override {
    return absl::OkStatus();
  };
  absl::Status sign(Http::RequestHeaderMap&, const std::string&,
                    const absl::string_view = "") override {
    return absl::OkStatus();
  };
};

class MockAuthContext : public ::grpc::AuthContext {
public:
  ~MockAuthContext() override = default;
  MOCK_METHOD(bool, IsPeerAuthenticated, (), (const, override));
  MOCK_METHOD(std::vector<grpc::string_ref>, GetPeerIdentity, (), (const, override));
  MOCK_METHOD(std::string, GetPeerIdentityPropertyName, (), (const, override));
  MOCK_METHOD(std::vector<grpc::string_ref>, FindPropertyValues, (const std::string& name),
              (const, override));
  MOCK_METHOD(::grpc::AuthPropertyIterator, begin, (), (const, override));
  MOCK_METHOD(::grpc::AuthPropertyIterator, end, (), (const, override));
  MOCK_METHOD(void, AddProperty, (const std::string& key, const grpc::string_ref& value),
              (override));
  MOCK_METHOD(bool, SetPeerIdentityPropertyName, (const std::string& name), (override));
};

TEST(GrpcAwsIamClientTest, AwsIamGrpcAuth_SignerError) {

  auto testAwsIamHeaderAuthenticator =
      Extensions::GrpcCredentials::AwsIam::AwsIamHeaderAuthenticator(
          std::make_unique<MockSigner>());

  std::string url = "https://a.example.org/test";
  grpc::string_ref urlGs(url.data(), url.size());
  std::string method = "POST";
  grpc::string_ref methodGs(method.data(), method.size());
  MockAuthContext context;
  std::multimap<grpc::string, grpc::string> metadata;
  auto status = testAwsIamHeaderAuthenticator.GetMetadata(urlGs, methodGs, context, &metadata);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
