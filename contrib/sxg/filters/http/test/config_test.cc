#include <memory>
#include <string>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/secret/secret_provider_impl.h"

#include "test/mocks/server/factory_context.h"

#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "contrib/sxg/filters/http/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

using testing::NiceMock;
using testing::Return;

namespace {

void expectCreateFilter(std::string yaml, bool is_sds_config) {
  FilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"foo"}, {});

  // This returns non-nullptr for certificate and private_key.
  auto& secret_manager =
      context.server_factory_context_.cluster_manager_.cluster_manager_factory_.secretManager();
  if (is_sds_config) {
    ON_CALL(secret_manager, findOrCreateGenericSecretProvider(_, _, _, _))
        .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
            envoy::extensions::transport_sockets::tls::v3::GenericSecret())));
  } else {
    ON_CALL(secret_manager, findStaticGenericSecretProvider(_))
        .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
            envoy::extensions::transport_sockets::tls::v3::GenericSecret())));
  }
  EXPECT_CALL(context, messageValidationVisitor());
  EXPECT_CALL(context.server_factory_context_, clusterManager());
  EXPECT_CALL(context, scope());
  EXPECT_CALL(context.server_factory_context_, timeSource());
  EXPECT_CALL(context.server_factory_context_, api());
  EXPECT_CALL(context, initManager()).Times(2);
  EXPECT_CALL(context, getTransportSocketFactoryContext());
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

// This loads one of the secrets in credentials, and fails the other one.
void expectInvalidSecretConfig(const std::string& failed_secret_name,
                               const std::string& exception_message) {
  const std::string yaml = R"YAML(
certificate:
  name: certificate
private_key:
  name: private_key
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
)YAML";

  FilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto& secret_manager =
      context.server_factory_context_.cluster_manager_.cluster_manager_factory_.secretManager();
  ON_CALL(secret_manager, findStaticGenericSecretProvider(
                              failed_secret_name == "private_key" ? "certificate" : "private_key"))
      .WillByDefault(Return(std::make_shared<Secret::GenericSecretConfigProviderImpl>(
          envoy::extensions::transport_sockets::tls::v3::GenericSecret())));

  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, exception_message);
}

} // namespace

TEST(ConfigTest, CreateFilterStaticSecretProvider) {
  const std::string yaml = R"YAML(
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
)YAML";
  expectCreateFilter(yaml, false);
}

TEST(ConfigTest, CreateFilterHasSdsSecret) {
  const std::string yaml = R"YAML(
certificate:
  name: certificate
  sds_config:
    path: "xxxx"
    resource_api_version: V3
private_key:
  name: private_key
  sds_config:
    path: "xxxx"
    resource_api_version: V3
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
)YAML";

  expectCreateFilter(yaml, true);
}

TEST(ConfigTest, InvalidCertificateSecret) {
  expectInvalidSecretConfig("certificate", "invalid certificate secret configuration");
}

TEST(ConfigTest, InvalidPrivateKeySecret) {
  expectInvalidSecretConfig("private_key", "invalid private_key secret configuration");
}

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
