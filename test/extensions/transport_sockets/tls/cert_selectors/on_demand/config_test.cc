#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"

#include "source/common/config/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {
namespace {

using StatusHelpers::StatusIs;
using ::testing::NiceMock;
using ::testing::Return;

class MockTlsCertificateSelectorContext : public Ssl::TlsCertificateSelectorContext {
public:
  ~MockTlsCertificateSelectorContext() override = default;
  MOCK_METHOD(const std::vector<Ssl::TlsContext>&, getTlsContexts, (), (const));
};

class OnDemandTest : public ::testing::Test {
protected:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr> create(const std::string& config_yaml,
                                                               bool for_quic = false) {
    envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
    TestUtility::loadFromYaml(config_yaml, config);
    Ssl::TlsCertificateSelectorConfigFactory& provider_factory =
        Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateSelectorConfigFactory>(
            "envoy.tls.certificate_selectors.on_demand_secret");
    EXPECT_CALL(server_context_, disableStatelessSessionResumption())
        .WillRepeatedly(Return(disable_stateless_resumption_));
    EXPECT_CALL(server_context_, disableStatefulSessionResumption())
        .WillRepeatedly(Return(disable_stateful_resumption_));
    return provider_factory.createTlsCertificateSelectorFactory(config, factory_context_,
                                                                server_context_, for_quic);
  }
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context_;
  NiceMock<Ssl::MockServerContextConfig> server_context_;
  NiceMock<MockTlsCertificateSelectorContext> selector_context_;

  std::string defaultConfig() const {
    return R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF";
  }

  std::string localSignerConfig() const {
    return R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        key_type: KEY_TYPE_RSA
        rsa_key_bits: 2048
        signature_hash: SIGNATURE_HASH_SHA256
        not_before_backdate_seconds: 60
        hostname_validation: HOSTNAME_VALIDATION_PERMISSIVE
        runtime_key_prefix: test.local_signer
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF";
  }

protected:
  bool disable_stateless_resumption_{true};
  bool disable_stateful_resumption_{true};
};

TEST_F(OnDemandTest, BasicLoadTest) { EXPECT_OK(create(defaultConfig())); }

TEST_F(OnDemandTest, BasicLoadTestLocalSigner) { EXPECT_OK(create(localSignerConfig())); }

TEST_F(OnDemandTest, BasicLoadTestQuic) {
  EXPECT_THAT(create(defaultConfig(), true), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, BasicLoadTestStatelessResumption) {
  disable_stateless_resumption_ = false;
  EXPECT_THAT(create(defaultConfig()), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, BasicLoadTestStatefulResumption) {
  disable_stateful_resumption_ = false;
  EXPECT_THAT(create(defaultConfig()), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, MustConfigureSourceOrLocalSigner) {
  EXPECT_THAT(create(R"EOF(
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerRequiresPaths) {
  EXPECT_THROW_WITH_REGEX(
      {
        auto ignored = create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF");
        (void)ignored;
      },
      ProtoValidationException,
      "Proto constraint validation failed.*CaKeyPath: value length must be at least 1 characters.*");
}

TEST_F(OnDemandTest, LocalSignerRejectsWeakRsaKeyBits) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        key_type: KEY_TYPE_RSA
        rsa_key_bits: 1024
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerRejectsUnknownSignatureHash) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        signature_hash: 99
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerRejectsUnknownEcdsaCurve) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        ecdsa_curve: 99
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerEcdsaConfigLoads) {
  EXPECT_OK(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        key_type: KEY_TYPE_ECDSA
        ecdsa_curve: ECDSA_CURVE_P384
        signature_hash: SIGNATURE_HASH_SHA384
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"));
}

TEST_F(OnDemandTest, LocalSignerRejectsUnknownKeyType) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        key_type: 99
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerRejectsUnknownHostnameValidation) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        hostname_validation: 99
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerRejectsUnknownCaReloadFailurePolicy) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        ca_reload_failure_policy: 99
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerFailOpenReloadPolicyLoads) {
  EXPECT_OK(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        ca_reload_failure_policy: CA_RELOAD_FAILURE_POLICY_FAIL_OPEN
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"));
}

TEST_F(OnDemandTest, LocalSignerRejectsUnknownKeyUsage) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        key_usages: [99]
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerRejectsUnknownExtendedKeyUsage) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        extended_key_usages: [99]
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerRejectsEmptyAdditionalDnsSan) {
  EXPECT_THAT(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        additional_dns_sans: [""]
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(OnDemandTest, LocalSignerPhase4ProfileConfigLoads) {
  EXPECT_OK(create(R"EOF(
      local_signer:
        ca_cert_path: /etc/envoy/mitm/ca.crt
        ca_key_path: /etc/envoy/mitm/ca.key
        include_primary_dns_san: true
        additional_dns_sans: ["api.internal.local", "api.mesh.local"]
        key_usages: [KEY_USAGE_DIGITAL_SIGNATURE, KEY_USAGE_KEY_ENCIPHERMENT]
        extended_key_usages: [EXTENDED_KEY_USAGE_SERVER_AUTH, EXTENDED_KEY_USAGE_CLIENT_AUTH]
        basic_constraints_ca: false
        subject_common_name: api.internal.local
        subject_organizational_unit: platform
        subject_country: US
        subject_state_or_province: CA
        subject_locality: SF
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF"));
}

TEST_F(OnDemandTest, QuicCall) {
  auto factory = create(defaultConfig());
  EXPECT_OK(factory);
  auto selector = factory.value()->create(selector_context_);
  bool sni;
  absl::InlinedVector<int, 3> curve;
  EXPECT_DEATH(selector->findTlsContext("", curve, false, &sni), "Not supported with QUIC");
}

TEST(FilterStateMapper, Derivation) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context;
  Ssl::UpstreamTlsCertificateMapperConfigFactory& mapper_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::UpstreamTlsCertificateMapperConfigFactory>(
          "envoy.tls.upstream_certificate_mappers.filter_state_override");
  envoy::extensions::transport_sockets::tls::cert_mappers::filter_state_override::v3::Config config;
  TestUtility::loadFromYaml("default_value: test", config);
  auto mapper_status = mapper_factory.createTlsCertificateMapperFactory(config, factory_context);
  ASSERT_OK(mapper_status);
  auto mapper = mapper_status.value()();
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  EXPECT_EQ("test", mapper->deriveFromServerHello(*ssl, nullptr));
  auto filter_state_object = std::make_shared<Router::StringAccessorImpl>("new_value");
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state.setData("envoy.tls.certificate_mappers.on_demand_secret", filter_state_object,
                       StreamInfo::FilterState::StateType::ReadOnly,
                       StreamInfo::FilterState::LifeSpan::Connection,
                       StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto transport_socket_options =
      Network::TransportSocketOptionsUtility::fromFilterState(filter_state);
  EXPECT_EQ("new_value", mapper->deriveFromServerHello(*ssl, transport_socket_options));
}

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
