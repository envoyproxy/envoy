#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/http/gcp_authn/filter_config.h"

#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;

TEST(GcpAuthnFilterConfigTest, DEPRECATED_FEATURE_TEST(GcpAuthnFilterWithCorrectProto)) {
  std::string filter_config_yaml = R"EOF(
    http_uri:
      uri: http://test/path
      cluster: test_cluster
      timeout:
        seconds: 5
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithNewProto) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FilterConfigTest, StandardConfigNoTokenBinding) {
  std::string config_yaml = R"EOF(
    cluster: test_cluster
    timeout:
      seconds: 5
  )EOF";
  GcpAuthnFilterConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  
  FilterConfig filter_config(proto_config, context);
  EXPECT_EQ(filter_config.clientCertFingerprint(), "");
  EXPECT_EQ(filter_config.protoConfig().cluster(), "test_cluster");
  EXPECT_EQ(filter_config.protoConfig().timeout().seconds(), 5);
}

TEST(FilterConfigTest, ConfigWithTokenBindingCertNotFound) {
  std::string config_yaml = R"EOF(
    cluster: test_cluster
    timeout:
      seconds: 5
    token_binding_config:
      client_certificate:
        name: missing_cert_secret
      client_certificate_san_matchers:
        - exact: "test.com"
  )EOF";
  GcpAuthnFilterConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  
  FilterConfig filter_config(proto_config, context);
  EXPECT_EQ(filter_config.clientCertFingerprint(), "");
}

TEST(FilterConfigTest, ConfigWithTokenBindingValidCert) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  
  const std::string cert_pem = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/non_spiffe_san_cert.pem"));

  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  secret.set_name("client_cert_secret");
  auto* tls_cert = secret.mutable_tls_certificate();
  tls_cert->mutable_certificate_chain()->set_inline_string(cert_pem);
  
  auto status = context.server_factory_context_.secretManager().addStaticSecret(secret);
  EXPECT_TRUE(status.ok());

  std::string config_yaml = R"EOF(
    cluster: test_cluster
    timeout:
      seconds: 5
    token_binding_config:
      client_certificate:
        name: client_cert_secret
      client_certificate_san_matchers:
        - exact: "test.com"
  )EOF";
  GcpAuthnFilterConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  
  FilterConfig filter_config(proto_config, context);
  EXPECT_FALSE(filter_config.clientCertFingerprint().empty());
}

TEST(FilterConfigTest, ConfigWithTokenBindingSanMismatch) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  
  const std::string cert_pem = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/non_spiffe_san_cert.pem"));

  envoy::extensions::transport_sockets::tls::v3::Secret secret;
  secret.set_name("client_cert_secret");
  auto* tls_cert = secret.mutable_tls_certificate();
  tls_cert->mutable_certificate_chain()->set_inline_string(cert_pem);
  
  auto status = context.server_factory_context_.secretManager().addStaticSecret(secret);
  EXPECT_TRUE(status.ok());

  std::string config_yaml = R"EOF(
    cluster: test_cluster
    timeout:
      seconds: 5
    token_binding_config:
      client_certificate:
        name: client_cert_secret
      client_certificate_san_matchers:
        - exact: "mismatch.com"
  )EOF";
  GcpAuthnFilterConfig proto_config;
  TestUtility::loadFromYaml(config_yaml, proto_config);
  
  FilterConfig filter_config(proto_config, context);
  EXPECT_TRUE(filter_config.clientCertFingerprint().empty());
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
