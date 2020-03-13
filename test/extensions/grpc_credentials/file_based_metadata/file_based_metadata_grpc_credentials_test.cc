#ifdef ENVOY_GOOGLE_GRPC

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/grpc_credential/v3/file_based_metadata.pb.h"

#include "common/common/fmt.h"
#include "common/grpc/google_async_client_impl.h"

#include "extensions/grpc_credentials/file_based_metadata/config.h"
#include "extensions/grpc_credentials/well_known_names.h"

#include "test/common/grpc/grpc_client_integration_test_harness.h"
#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Grpc {
namespace {

// FileBasedMetadata credential validation tests.
class GrpcFileBasedMetadataClientIntegrationTest : public GrpcSslClientIntegrationTest {
public:
  void expectExtraHeaders(FakeStream& fake_stream) override {
    AssertionResult result = fake_stream.waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    Http::TestHeaderMapImpl stream_headers(fake_stream.headers());
    if (!header_value_1_.empty()) {
      EXPECT_EQ(header_prefix_1_ + header_value_1_, stream_headers.get_(header_key_1_));
    }
    if (!header_value_2_.empty()) {
      EXPECT_EQ(header_value_2_, stream_headers.get_("authorization"));
    }
  }

  envoy::config::core::v3::GrpcService createGoogleGrpcConfig() override {
    auto config = GrpcClientIntegrationTest::createGoogleGrpcConfig();
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->set_credentials_factory_name(credentials_factory_name_);
    auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
    ssl_creds->mutable_root_certs()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    if (!header_value_1_.empty()) {
      const std::string yaml1 = fmt::format(R"EOF(
"@type": type.googleapis.com/envoy.config.grpc_credential.v2alpha.FileBasedMetadataConfig        
secret_data:
  inline_string: {}
header_key: {}
header_prefix: {}
)EOF",
                                            header_value_1_, header_key_1_, header_prefix_1_);
      auto* plugin_config = google_grpc->add_call_credentials()->mutable_from_plugin();
      plugin_config->set_name(credentials_factory_name_);
      envoy::config::grpc_credential::v3::FileBasedMetadataConfig metadata_config;
      Envoy::TestUtility::loadFromYaml(yaml1, *plugin_config->mutable_typed_config());
    }
    if (!header_value_2_.empty()) {
      // uses default key/prefix
      const std::string yaml2 = fmt::format(R"EOF(
"@type": type.googleapis.com/envoy.config.grpc_credential.v2alpha.FileBasedMetadataConfig        
secret_data:
  inline_string: {}
)EOF",
                                            header_value_2_);
      envoy::config::grpc_credential::v3::FileBasedMetadataConfig metadata_config2;
      auto* plugin_config2 = google_grpc->add_call_credentials()->mutable_from_plugin();
      plugin_config2->set_name(credentials_factory_name_);
      Envoy::TestUtility::loadFromYaml(yaml2, *plugin_config2->mutable_typed_config());
    }
    if (!access_token_value_.empty()) {
      google_grpc->add_call_credentials()->set_access_token(access_token_value_);
    }
    return config;
  }

  std::string header_key_1_{};
  std::string header_value_1_{};
  std::string header_value_2_{};
  std::string header_prefix_1_{};
  std::string access_token_value_{};
  std::string credentials_factory_name_{};
};

// Parameterize the loopback test server socket address and gRPC client type.
INSTANTIATE_TEST_SUITE_P(SslIpVersionsClientType, GrpcFileBasedMetadataClientIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Validate that a simple request-reply unary RPC works with FileBasedMetadata auth.
TEST_P(GrpcFileBasedMetadataClientIntegrationTest, FileBasedMetadataGrpcAuthRequest) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  header_key_1_ = "header1";
  header_prefix_1_ = "prefix1";
  header_value_1_ = "secretvalue";
  credentials_factory_name_ =
      Extensions::GrpcCredentials::GrpcCredentialsNames::get().FileBasedMetadata;
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that two separate metadata plugins work with FileBasedMetadata auth.
TEST_P(GrpcFileBasedMetadataClientIntegrationTest, DoubleFileBasedMetadataGrpcAuthRequest) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  header_key_1_ = "header1";
  header_prefix_1_ = "prefix1";
  header_value_1_ = "secretvalue";
  header_value_2_ = "secret2";
  credentials_factory_name_ =
      Extensions::GrpcCredentials::GrpcCredentialsNames::get().FileBasedMetadata;
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that FileBasedMetadata auth plugin works without a config loaded
TEST_P(GrpcFileBasedMetadataClientIntegrationTest, EmptyFileBasedMetadataGrpcAuthRequest) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  credentials_factory_name_ =
      Extensions::GrpcCredentials::GrpcCredentialsNames::get().FileBasedMetadata;
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

// Validate that FileBasedMetadata auth plugin works with extra credentials configured
TEST_P(GrpcFileBasedMetadataClientIntegrationTest, ExtraConfigFileBasedMetadataGrpcAuthRequest) {
  SKIP_IF_GRPC_CLIENT(ClientType::EnvoyGrpc);
  access_token_value_ = "testaccesstoken";
  header_key_1_ = "header1";
  header_prefix_1_ = "prefix1";
  header_value_1_ = "secretvalue";
  credentials_factory_name_ =
      Extensions::GrpcCredentials::GrpcCredentialsNames::get().FileBasedMetadata;
  initialize();
  auto request = createRequest(empty_metadata_);
  request->sendReply();
  dispatcher_helper_.runDispatcher();
}

class MockAuthContext : public ::grpc::AuthContext {
public:
  ~MockAuthContext() override {}
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

TEST(GrpcFileBasedMetadata, MissingSecretData) {
  const std::string yaml = R"EOF(
secret_data:
  filename: missing-file
)EOF";
  envoy::config::grpc_credential::v3::FileBasedMetadataConfig metadata_config;
  Envoy::TestUtility::loadFromYaml(yaml, metadata_config);
  Api::ApiPtr api = Api::createApiForTest();
  Extensions::GrpcCredentials::FileBasedMetadata::FileBasedMetadataAuthenticator authenticator(
      metadata_config, *api);

  MockAuthContext context;
  std::multimap<grpc::string, grpc::string> metadata;
  auto status =
      authenticator.GetMetadata(grpc::string_ref(), grpc::string_ref(), context, &metadata);
  EXPECT_EQ(grpc::StatusCode::NOT_FOUND, status.error_code());
}

} // namespace
} // namespace Grpc
} // namespace Envoy
#endif
