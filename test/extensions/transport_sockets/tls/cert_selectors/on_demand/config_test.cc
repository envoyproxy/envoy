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
#include "test/test_common/test_runtime.h"
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
using ::testing::_;
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

protected:
  bool disable_stateless_resumption_{true};
  bool disable_stateful_resumption_{true};
};

TEST_F(OnDemandTest, BasicLoadTest) { EXPECT_OK(create(defaultConfig())); }

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
                       StreamInfo::FilterState::LifeSpan::Connection,
                       StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto transport_socket_options =
      Network::TransportSocketOptionsUtility::fromFilterState(filter_state);
  EXPECT_EQ("new_value", mapper->deriveFromServerHello(*ssl, transport_socket_options));
}

TEST(SNIMapperTest, ValidationAndNormalization) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context;
  Ssl::TlsCertificateMapperConfigFactory& mapper_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateMapperConfigFactory>(
          "envoy.tls.certificate_mappers.sni");
  envoy::extensions::transport_sockets::tls::cert_mappers::sni::v3::SNI config;
  // Verify operator-configured default_value with underscores bypasses strict DNS validation.
  TestUtility::loadFromYaml("default_value: envoy_proxy_server_cert", config);
  auto mapper_status = mapper_factory.createTlsCertificateMapperFactory(config, factory_context);
  ASSERT_OK(mapper_status);
  auto mapper = mapper_status.value()();

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  SSL_CLIENT_HELLO client_hello{};
  client_hello.ssl = ssl.get();

  // Absent SNI returns configured default_value unchanged.
  EXPECT_EQ("envoy_proxy_server_cert", mapper->deriveFromClientHello(client_hello));

  // Valid SNIs are normalized (converted to lowercase, optional single trailing dot stripped).
  auto derive_for = [&](const char* sni) -> std::string {
    EXPECT_EQ(1, SSL_set_tlsext_host_name(ssl.get(), sni));
    return mapper->deriveFromClientHello(client_hello);
  };
  EXPECT_EQ("server", derive_for("server"));
  EXPECT_EQ("localhost", derive_for("LOCALHOST"));
  EXPECT_EQ("example.com", derive_for("Example.COM"));
  EXPECT_EQ("example.com", derive_for("Example.COM."));
  EXPECT_EQ("xn--bcher-kva.example", derive_for("XN--BCHER-KVA.EXAMPLE"));

  // Invalid SNIs return empty string so on_demand_secret fails certificate selection closed.
  EXPECT_EQ("", derive_for("*"));
  EXPECT_EQ("", derive_for("*.example.com"));
  EXPECT_EQ("", derive_for("xdstp:x"));
  EXPECT_EQ("", derive_for("xdstp://x"));
  EXPECT_EQ("", derive_for("127.0.0.1"));
  EXPECT_EQ("", derive_for("::1"));
  EXPECT_EQ("", derive_for("foo_bar.example.com"));
  EXPECT_EQ("", derive_for("foo\nbar.example.com"));
  EXPECT_EQ("", derive_for("\xff\xfe.example"));
  EXPECT_EQ("", derive_for("-bad.example.com"));
  EXPECT_EQ("", derive_for("example.com.."));

  // Runtime guard allows reverting strict hostname validation in an emergency.
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.tls_sni_mapper_validate_hostname", "false"}});
  EXPECT_EQ("Example.COM.", derive_for("Example.COM."));
  EXPECT_EQ("foo_bar.example.com", derive_for("foo_bar.example.com"));
}

class MockCertificateSelectionCallback : public Ssl::CertificateSelectionCallback {
public:
  explicit MockCertificateSelectionCallback(Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {}
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void onCertificateSelectionResult(OptRef<const Ssl::TlsContext> ctx, bool staple) override {
    called_ = true;
    has_ctx_ = ctx.has_value();
    staple_ = staple;
  }

  Event::Dispatcher& dispatcher_;
  bool called_{false};
  bool has_ctx_{false};
  bool staple_{false};
};

TEST_F(OnDemandTest, SelectorRejectsInvalidSni) {
  auto factory = create(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: sni
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
        default_value: default-sni
  )EOF");
  ASSERT_OK(factory);
  auto selector = factory.value()->create(selector_context_);

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  SSL_CLIENT_HELLO client_hello{};
  client_hello.ssl = ssl.get();

  // Do not post any SDS fetches to mainThreadDispatcher when SNIMapper returns "".
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_)).Times(0);

  for (const char* bad_sni :
       {"xdstp:x", "xdstp://x", "*", "127.0.0.1", "foo\nbar", "\xff\xfe.x", "a_b.example.com"}) {
    ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl.get(), bad_sni));
    auto cb = std::make_unique<MockCertificateSelectionCallback>(
        factory_context_.server_context_.dispatcher_);
    const auto result = selector->selectTlsContext(client_hello, std::move(cb));
    EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Failed, result.status) << bad_sni;
  }
}

TEST_F(OnDemandTest, UpstreamSelectorRejectsEmptyFilterStateOverride) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: filter_state_override
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.filter_state_override.v3.Config
        default_value: fallback
  )EOF",
                            config);
  NiceMock<Ssl::MockClientContextConfig> client_context;
  auto& provider_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::UpstreamTlsCertificateSelectorConfigFactory>(
          "envoy.tls.certificate_selectors.on_demand_secret");
  auto factory = provider_factory.createUpstreamTlsCertificateSelectorFactory(
      config, factory_context_, client_context);
  ASSERT_OK(factory);
  auto selector = factory.value()->createUpstreamTlsCertificateSelector(selector_context_);

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_)).Times(0);

  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state.setData("envoy.tls.certificate_mappers.on_demand_secret",
                       std::make_shared<Router::StringAccessorImpl>(""),
                       StreamInfo::FilterState::LifeSpan::Connection,
                       StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  auto options = Network::TransportSocketOptionsUtility::fromFilterState(filter_state);
  auto cb = std::make_unique<MockCertificateSelectionCallback>(
      factory_context_.server_context_.dispatcher_);
  const auto result = selector->selectTlsContext(*ssl, options, std::move(cb));
  EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Failed, result.status);
}

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
