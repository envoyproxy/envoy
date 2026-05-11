// Integration test verifying that SDS-based generic secret rotation is reflected in
// access log output via the substitution formatter.

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class GenericSecretRotationIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  GenericSecretRotationIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initialize() override {
    // Write the initial SDS YAML with the secret as an inline_string.
    writeSdsYaml("initial-token");
    sds_yaml_path_ = TestEnvironment::temporaryPath("secret_token_sds.yaml");

    // Build the generic_secret formatter extension config.
    envoy::config::core::v3::TypedExtensionConfig formatter_ext;
    formatter_ext.set_name("envoy.formatter.generic_secret");
    envoy::extensions::formatter::generic_secret::v3::GenericSecret generic_secret_cfg;
    auto& secret_cfg = (*generic_secret_cfg.mutable_secret_configs())["api-token"];
    secret_cfg.set_name("api-token");
    secret_cfg.mutable_sds_config()->mutable_path_config_source()->set_path(sds_yaml_path_);
    formatter_ext.mutable_typed_config()->PackFrom(generic_secret_cfg);

    useAccessLog("%SECRET(api-token)%", {formatter_ext});
    HttpIntegrationTest::initialize();
  }

  static std::string sdsYaml(const std::string& token) {
    return fmt::format(R"EOF(
resources:
- "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
  name: api-token
  generic_secret:
    secret:
      inline_string: "{}"
)EOF",
                       token);
  }

  // Write an SDS YAML file with the given token value.
  static void writeSdsYaml(const std::string& token) {
    TestEnvironment::writeStringToFileForTest("secret_token_sds.yaml", sdsYaml(token));
  }

  // Rotate the secret by atomically replacing the SDS YAML file.
  // FilesystemSubscriptionImpl watches for MovedTo (rename) events only, so we
  // write to a temp file and rename to trigger the watcher.
  void rotateSecret(const std::string& new_token) {
    TestEnvironment::writeStringToFileForTest("secret_token_sds.yaml.tmp", sdsYaml(new_token));
    TestEnvironment::renameFile(TestEnvironment::temporaryPath("secret_token_sds.yaml.tmp"),
                                TestEnvironment::temporaryPath("secret_token_sds.yaml"));
  }

  std::string sds_yaml_path_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GenericSecretRotationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const testing::TestParamInfo<Network::Address::IpVersion>& info) {
                           return TestUtility::ipVersionToString(info.param);
                         });

TEST_P(GenericSecretRotationIntegrationTest, SecretRotationReflectedInAccessLog) {
  autonomous_upstream_ = true;
  initialize();

  // Trigger the first access log entry.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("initial-token", waitForAccessLog(access_log_name_));

  // Rotate the secret and wait for the SDS update to propagate.
  rotateSecret("rotated-token");
  test_server_->waitForCounter("sds.api-token.update_success", testing::Ge(2));

  // Trigger a second access log entry; the flush will use the rotated secret value.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("rotated-token", waitForAccessLog(access_log_name_, 1));

  codec_client_->close();
}

// Integration test verifying that static bootstrap generic secrets are resolved
// by the %SECRET(name)% formatter.
class GenericSecretStaticIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  GenericSecretStaticIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initialize() override {
    // Add a static generic secret to the bootstrap config.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* secret = bootstrap.mutable_static_resources()->add_secrets();
      secret->set_name("static-token");
      secret->mutable_generic_secret()->mutable_secret()->set_inline_string("my-static-value");
    });

    // Build the generic_secret formatter extension config referencing the static secret
    // (no sds_config means the static provider path is used).
    envoy::config::core::v3::TypedExtensionConfig formatter_ext;
    formatter_ext.set_name("envoy.formatter.generic_secret");
    envoy::extensions::formatter::generic_secret::v3::GenericSecret generic_secret_cfg;
    (*generic_secret_cfg.mutable_secret_configs())["api-token"].set_name("static-token");
    formatter_ext.mutable_typed_config()->PackFrom(generic_secret_cfg);

    useAccessLog("%SECRET(api-token)%", {formatter_ext});
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GenericSecretStaticIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const testing::TestParamInfo<Network::Address::IpVersion>& info) {
                           return TestUtility::ipVersionToString(info.param);
                         });

TEST_P(GenericSecretStaticIntegrationTest, StaticSecretResolvedInAccessLog) {
  autonomous_upstream_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("my-static-value", waitForAccessLog(access_log_name_));

  codec_client_->close();
}

// Integration test verifying that referencing a secret name in the format string
// that is not listed in secret_configs causes a config rejection.
class GenericSecretUnknownNameIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  GenericSecretUnknownNameIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* secret = bootstrap.mutable_static_resources()->add_secrets();
      secret->set_name("known-token");
      secret->mutable_generic_secret()->mutable_secret()->set_inline_string("value");
    });

    // Reference "unknown-token" in the format string but only configure "known-token"
    // in secret_configs.
    envoy::config::core::v3::TypedExtensionConfig formatter_ext;
    formatter_ext.set_name("envoy.formatter.generic_secret");
    envoy::extensions::formatter::generic_secret::v3::GenericSecret generic_secret_cfg;
    (*generic_secret_cfg.mutable_secret_configs())["known-token"].set_name("known-token");
    formatter_ext.mutable_typed_config()->PackFrom(generic_secret_cfg);

    useAccessLog("%SECRET(unknown-token)%", {formatter_ext});
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GenericSecretUnknownNameIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const testing::TestParamInfo<Network::Address::IpVersion>& info) {
                           return TestUtility::ipVersionToString(info.param);
                         });

TEST_P(GenericSecretUnknownNameIntegrationTest, UnknownSecretNameRejected) {
  EXPECT_DEATH(initialize(), "Lds update failed");
}

} // namespace
} // namespace Envoy
