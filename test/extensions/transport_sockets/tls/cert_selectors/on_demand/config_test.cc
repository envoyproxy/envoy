#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/config/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
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

TEST_F(OnDemandTest, ValidCacheLimitsConfig) {
  const std::string config_yaml = R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_active_secrets: 100
    max_pending_secrets: 16
  )EOF";
  EXPECT_OK(create(config_yaml));
}

TEST_F(OnDemandTest, InvalidZeroMaxActiveSecrets) {
  const std::string config_yaml = R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_active_secrets: 0
  )EOF";
  EXPECT_THROW_WITH_REGEX(create(config_yaml).IgnoreError(), EnvoyException,
                          "MaxActiveSecrets.*greater than 0");
}

TEST_F(OnDemandTest, InvalidZeroMaxPendingSecrets) {
  const std::string config_yaml = R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_pending_secrets: 0
  )EOF";
  EXPECT_THROW_WITH_REGEX(create(config_yaml).IgnoreError(), EnvoyException,
                          "MaxPendingSecrets.*greater than 0");
}

TEST_F(OnDemandTest, StaleQueuedRemovalDoesNotEraseReinsertedEntry) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(defaultConfig(), config);

  std::vector<Event::PostCb> queued_posts;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillRepeatedly([&queued_posts](Event::PostCb cb) { queued_posts.push_back(std::move(cb)); });

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope&, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&, absl::Status&) { return nullptr; });

  auto gauge_value = [&](absl::string_view name) {
    return factory_context_.store_.gauge(std::string(name), Stats::Gauge::ImportMode::Accumulate)
        .value();
  };

  // Generation 1 inserted.
  secret_manager->addCertificateConfig("example.com", nullptr, {});
  EXPECT_EQ(1, gauge_value("on_demand_secret.cert_active"));
  EXPECT_EQ(1, gauge_value("on_demand_secret.cert_pending"));

  // Enqueue two pending removals targeting generation 1 (one simulates the stale SDS post queued
  // just before eviction/removal; the second retires generation 1).
  EXPECT_OK(secret_manager->removeCertificateConfig("example.com", 1));
  EXPECT_OK(secret_manager->removeCertificateConfig("example.com", 1));
  ASSERT_EQ(2, queued_posts.size());
  auto stale_gen1_removal = std::move(queued_posts[0]);
  auto active_gen1_removal = std::move(queued_posts[1]);
  queued_posts.clear();

  // Retire generation 1 and immediately re-insert the same name as generation 2.
  active_gen1_removal();
  EXPECT_EQ(0, gauge_value("on_demand_secret.cert_active"));
  EXPECT_EQ(0, gauge_value("on_demand_secret.cert_pending"));

  secret_manager->addCertificateConfig("example.com", nullptr, {});
  EXPECT_EQ(1, gauge_value("on_demand_secret.cert_active"));
  EXPECT_EQ(1, gauge_value("on_demand_secret.cert_pending"));

  // Executing the stale queued removal from generation 1 must be a no-op (ABA guard).
  stale_gen1_removal();
  EXPECT_EQ(1, gauge_value("on_demand_secret.cert_active"));
  EXPECT_EQ(1, gauge_value("on_demand_secret.cert_pending"));

  // A removal carrying generation 2 removes the live entry as expected.
  EXPECT_OK(secret_manager->removeCertificateConfig("example.com", 2));
  ASSERT_EQ(1, queued_posts.size());
  queued_posts[0]();
  EXPECT_EQ(0, gauge_value("on_demand_secret.cert_active"));
  EXPECT_EQ(0, gauge_value("on_demand_secret.cert_pending"));
}

class FakeAsyncContext : public AsyncContext {
public:
  explicit FakeAsyncContext(Stats::Scope& scope) : AsyncContext(scope) {}
  Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const override {
    return Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling;
  }
  const Ssl::TlsContext& tlsContext() const override { return tls_context_; }

private:
  Ssl::TlsContext tls_context_;
};

class MockCertificateSelectionCallback : public Ssl::CertificateSelectionCallback {
public:
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, onCertificateSelectionResult,
              (OptRef<const Ssl::TlsContext> selected_ctx, bool staple));
};

std::string boundedConfig() {
  return R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_active_secrets: 2
    max_pending_secrets: 2
  )EOF";
}

TEST_F(OnDemandTest, UpdateAndRemoveUnknownSecretAreNoops) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(defaultConfig(), config);

  std::vector<Event::PostCb> queued_posts;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillRepeatedly([&queued_posts](Event::PostCb cb) { queued_posts.push_back(std::move(cb)); });

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });

  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  EXPECT_OK(secret_manager->updateCertificate("unknown.com", cert_config));
  EXPECT_FALSE(secret_manager->getContext("unknown.com").has_value());

  EXPECT_OK(secret_manager->removeCertificateConfig("unknown.com"));
  ASSERT_EQ(1, queued_posts.size());
  queued_posts[0]();
  EXPECT_EQ(0, factory_context_.store_
                   .gauge("on_demand_secret.cert_active", Stats::Gauge::ImportMode::Accumulate)
                   .value());
}

TEST_F(OnDemandTest, MainThreadFetchOfActiveSecretNotifiesImmediately) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(boundedConfig(), config);

  std::vector<Event::PostCb> queued_posts;
  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([&queued_posts](Event::PostCb cb) {
    queued_posts.push_back(std::move(cb));
  });

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  EXPECT_TRUE(secret_manager->trackingEnabled());

  // Activate the secret via a prefetch-style subscription.
  secret_manager->addCertificateConfig("example.com", nullptr, {});
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  EXPECT_OK(secret_manager->updateCertificate("example.com", cert_config));
  ASSERT_TRUE(secret_manager->getContext("example.com").has_value());

  // A main-thread fetch for an already active secret (e.g. a worker missed the thread local
  // update) records a hit and notifies the handle without starting a new subscription.
  auto cb = std::make_unique<NiceMock<MockCertificateSelectionCallback>>();
  ON_CALL(*cb, dispatcher()).WillByDefault(::testing::ReturnRef(dispatcher));
  EXPECT_CALL(
      *cb, onCertificateSelectionResult(
               ::testing::Truly([](OptRef<const Ssl::TlsContext> ctx) { return ctx.has_value(); }),
               false));
  auto handle = std::make_shared<Handle>(std::move(cb), false);
  queued_posts.clear();
  secret_manager->addCertificateConfig("example.com", handle, {},
                                       FairSieve::computeTenantId("tenant-a"));
  ASSERT_EQ(1, queued_posts.size());
  queued_posts[0]();

  EXPECT_EQ(1, factory_context_.store_.counter("on_demand_secret.cert_requested").value());
  EXPECT_EQ(1, factory_context_.store_
                   .gauge("on_demand_secret.cert_active", Stats::Gauge::ImportMode::Accumulate)
                   .value());
}

TEST_F(OnDemandTest, DownstreamTenantExtraction) {
  std::vector<Event::PostCb> queued_posts;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillRepeatedly([&queued_posts](Event::PostCb cb) { queued_posts.push_back(std::move(cb)); });

  auto factory = create(boundedConfig());
  ASSERT_OK(factory);
  auto selector = factory.value()->create(selector_context_);

  auto select = [&](SSL* ssl) {
    SSL_CLIENT_HELLO hello{};
    hello.ssl = ssl;
    auto result = selector->selectTlsContext(
        hello, std::make_unique<NiceMock<MockCertificateSelectionCallback>>());
    EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Pending, result.status);
  };

  // No SSL object.
  select(nullptr);

  // SSL object without transport socket callbacks.
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  select(ssl.get());

  // Transport socket callbacks without a filter state.
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  ASSERT_EQ(1, SSL_set_ex_data(ssl.get(), ContextImpl::sslSocketIndex(), &callbacks));
  callbacks.connection_.stream_info_.filter_state_ = nullptr;
  select(ssl.get());

  // Filter state without and with a tenant identifier.
  callbacks.connection_.stream_info_.filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  select(ssl.get());
  callbacks.connection_.stream_info_.filter_state_->setData(
      TenantIdFilterStateKey, std::make_shared<Router::StringAccessorImpl>("tenant-a"),
      StreamInfo::FilterState::LifeSpan::Connection);
  select(ssl.get());
  SSL_set_ex_data(ssl.get(), ContextImpl::sslSocketIndex(), nullptr);

  EXPECT_EQ(5, queued_posts.size());
}

TEST_F(OnDemandTest, UpstreamTenantExtraction) {
  std::vector<Event::PostCb> queued_posts;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillRepeatedly([&queued_posts](Event::PostCb cb) { queued_posts.push_back(std::move(cb)); });

  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: filter-state-override
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.filter_state_override.v3.Config
        default_value: server
    max_pending_secrets: 4
  )EOF",
                            config);
  Ssl::UpstreamTlsCertificateSelectorConfigFactory& provider_factory =
      Config::Utility::getAndCheckFactoryByName<Ssl::UpstreamTlsCertificateSelectorConfigFactory>(
          "envoy.tls.certificate_selectors.on_demand_secret");
  NiceMock<Ssl::MockClientContextConfig> client_context;
  auto factory = provider_factory.createUpstreamTlsCertificateSelectorFactory(
      config, factory_context_, client_context);
  ASSERT_OK(factory);
  auto selector = factory.value()->createUpstreamTlsCertificateSelector(selector_context_);

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  auto select = [&](const Network::TransportSocketOptionsConstSharedPtr& options) {
    auto result = selector->selectTlsContext(
        *ssl, options, std::make_unique<NiceMock<MockCertificateSelectionCallback>>());
    EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Pending, result.status);
  };

  // No transport socket options: fall back to the SSL object.
  select(nullptr);

  // Shared tenant object of an unexpected type: fall back to the SSL object.
  {
    StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
    filter_state.setData(TenantIdFilterStateKey,
                         std::make_shared<StreamInfo::UInt32AccessorImpl>(42),
                         StreamInfo::FilterState::LifeSpan::Connection,
                         StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    select(Network::TransportSocketOptionsUtility::fromFilterState(filter_state));
  }

  // Shared string tenant object.
  {
    StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
    filter_state.setData(TenantIdFilterStateKey,
                         std::make_shared<Router::StringAccessorImpl>("tenant-a"),
                         StreamInfo::FilterState::LifeSpan::Connection,
                         StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    select(Network::TransportSocketOptionsUtility::fromFilterState(filter_state));
  }

  EXPECT_EQ(3, queued_posts.size());
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

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
