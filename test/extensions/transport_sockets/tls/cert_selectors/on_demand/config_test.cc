#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/init/manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/fair_sieve_cache.h"
#include "source/common/config/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/stats/allocator.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/common/async_context.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"
#include "source/server/generic_factory_context.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "spdlog/common.h"

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

class FakeTlsCertificateConfigProvider : public Secret::TlsCertificateConfigProvider {
public:
  const envoy::extensions::transport_sockets::tls::v3::TlsCertificate* secret() const override {
    return has_secret_ ? &secret_ : nullptr;
  }

  ::Envoy::Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)>) override {
    return nullptr;
  }

  ::Envoy::Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()> callback) override {
    return update_callbacks_.add(std::move(callback));
  }

  ::Envoy::Common::CallbackHandlePtr
  addRemoveCallback(std::function<absl::Status()> callback) override {
    return remove_callbacks_.add(std::move(callback));
  }

  void start() override {}

  absl::Status deliverSecret() {
    secret_.mutable_certificate_chain()->set_inline_string("fake-cert-pem");
    secret_.mutable_private_key()->set_inline_string("fake-key-pem");
    has_secret_ = true;
    return update_callbacks_.runCallbacks();
  }

private:
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate secret_;
  bool has_secret_{false};
  ::Envoy::Common::CallbackManager<absl::Status> update_callbacks_;
  ::Envoy::Common::CallbackManager<absl::Status> remove_callbacks_;
};

class CountingFilterState : public StreamInfo::FilterStateImpl {
public:
  explicit CountingFilterState(FilterState::LifeSpan life_span)
      : StreamInfo::FilterStateImpl(life_span) {}

  const Object* getDataReadOnlyGeneric(absl::string_view data_name) const override {
    ++read_lookups_;
    return StreamInfo::FilterStateImpl::getDataReadOnlyGeneric(data_name);
  }

  size_t readLookups() const { return read_lookups_; }
  void resetLookups() const { read_lookups_ = 0; }

private:
  mutable size_t read_lookups_{0};
};

class OnDemandSecretManagerTest : public Event::TestUsingSimulatedTime, public OnDemandTest {
protected:
  void TearDown() override {
    factory_context_.server_context_.dispatcher_.clearDeferredDeleteList();
  }

  std::string boundedConfig() const {
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

  uint64_t gaugeValue(absl::string_view name) {
    return factory_context_.store_.gauge(std::string(name), Stats::Gauge::ImportMode::Accumulate)
        .value();
  }

  uint64_t counterValue(absl::string_view name) {
    return factory_context_.store_.counter(std::string(name)).value();
  }

  std::shared_ptr<FakeTlsCertificateConfigProvider>
  registerFakeProvider(absl::string_view secret_name) {
    auto provider = std::make_shared<FakeTlsCertificateConfigProvider>();
    ON_CALL(factory_context_.server_context_, secretManager())
        .WillByDefault(::testing::ReturnRef(mock_secret_manager_));
    ON_CALL(mock_secret_manager_, findOrCreateTlsCertificateProvider(_, _, _, _, _))
        .WillByDefault([provider, target = std::string(secret_name)](
                           const envoy::config::core::v3::ConfigSource&, const std::string& name,
                           Server::Configuration::ServerFactoryContext&, OptRef<Init::Manager>,
                           bool) -> Secret::TlsCertificateConfigProviderSharedPtr {
          if (name == target) {
            return provider;
          }
          return std::make_shared<FakeTlsCertificateConfigProvider>();
        });
    return provider;
  }

  NiceMock<Secret::MockSecretManager> mock_secret_manager_;
};

TEST_F(OnDemandSecretManagerTest, UpdateAndRemoveUnknownSecretAreNoops) {
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
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_active"));
}

TEST_F(OnDemandSecretManagerTest, MainThreadFetchOfActiveSecretNotifiesImmediately) {
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

  EXPECT_EQ(1, counterValue("on_demand_secret.cert_requested"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_active"));
}

TEST_F(OnDemandSecretManagerTest, DownstreamTenantExtraction) {
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

TEST_F(OnDemandSecretManagerTest, UpstreamTenantExtraction) {
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

TEST_F(OnDemandSecretManagerTest, UpdateAllDoesNotPromoteFailedPendingEntryOrBypassLimit) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(boundedConfig(), config);

  std::vector<Event::PostCb> queued_posts;
  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([&queued_posts](Event::PostCb cb) {
    queued_posts.push_back(std::move(cb));
  });

  auto provider = registerFakeProvider("example.com");
  bool fail_factory = true;
  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [&fail_factory](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
                      const Ssl::TlsCertificateConfig&, absl::Status& status) {
        if (fail_factory) {
          status = absl::InvalidArgumentError("parent tls config mismatch");
        }
        return std::make_shared<FakeAsyncContext>(scope);
      });

  auto cb = std::make_unique<NiceMock<MockCertificateSelectionCallback>>();
  ON_CALL(*cb, dispatcher()).WillByDefault(::testing::ReturnRef(dispatcher));
  auto handle = std::make_shared<Handle>(std::move(cb), false);
  secret_manager->addCertificateConfig("example.com", handle, {});
  ASSERT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));

  EXPECT_FALSE(provider->deliverSecret().ok());
  ASSERT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));
  ASSERT_FALSE(secret_manager->getContext("example.com").has_value());

  fail_factory = false;
  EXPECT_OK(secret_manager->updateAll());
  EXPECT_FALSE(secret_manager->getContext("example.com").has_value());

  // Activate 2 additional secrets; max_active_secrets=2 must still hold.
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  for (const char* name : {"s2", "s3"}) {
    secret_manager->addCertificateConfig(name, nullptr, {});
    EXPECT_OK(secret_manager->updateCertificate(name, cert_config));
  }
  size_t active_in_tls = 0;
  for (const char* name : {"example.com", "s2", "s3"}) {
    if (secret_manager->getContext(name).has_value()) {
      ++active_in_tls;
    }
  }
  EXPECT_EQ(2, active_in_tls);

  EXPECT_OK(secret_manager->removeCertificateConfig("example.com"));
  for (auto& post : queued_posts) {
    post();
  }
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
}

TEST_F(OnDemandSecretManagerTest, UpdateAllDoesNotBroadcastBrokenContextOnFailure) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(defaultConfig(), config);

  auto provider = registerFakeProvider("example.com");
  bool fail_factory = false;
  AsyncContextSharedPtr good_ctx;
  AsyncContextSharedPtr broken_ctx;
  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [&](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
          const Ssl::TlsCertificateConfig&, absl::Status& status) {
        auto ctx = std::make_shared<FakeAsyncContext>(scope);
        if (fail_factory) {
          status = absl::InvalidArgumentError("broken context");
          broken_ctx = ctx;
        } else {
          good_ctx = ctx;
        }
        return ctx;
      });

  secret_manager->addCertificateConfig("example.com", nullptr, {});
  ASSERT_OK(provider->deliverSecret());
  ASSERT_EQ(good_ctx, secret_manager->getContext("example.com").value_or(nullptr));

  fail_factory = true;
  EXPECT_FALSE(secret_manager->updateAll().ok());
  ASSERT_NE(nullptr, broken_ctx);
  EXPECT_EQ(good_ctx, secret_manager->getContext("example.com").value_or(nullptr));
}

TEST_F(OnDemandSecretManagerTest, ServerAsyncContextReturnsNonNullOnInvalidPem) {
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate cert_proto;
  cert_proto.mutable_certificate_chain()->set_inline_string("not-a-valid-pem-cert");
  cert_proto.mutable_private_key()->set_inline_string("not-a-valid-pem-key");
  Server::GenericFactoryContextImpl generic_context(factory_context_.server_context_,
                                                    factory_context_.messageValidationVisitor());
  auto cert_config_or = Ssl::TlsCertificateConfigImpl::create(
      cert_proto, generic_context, factory_context_.server_context_.api(), "bad.example");
  ASSERT_OK(cert_config_or);

  absl::Status creation_status = absl::OkStatus();
  auto ctx = std::make_shared<ServerAsyncContext>(*factory_context_.store_.rootScope(),
                                                  factory_context_.server_context_, server_context_,
                                                  *cert_config_or, creation_status);
  EXPECT_FALSE(creation_status.ok());
  EXPECT_NE(nullptr, ctx);
}

TEST_F(OnDemandSecretManagerTest, DestroySecretManagerReleasesAccumulateGauges) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(boundedConfig(), config);

  {
    auto secret_manager = std::make_shared<SecretManager>(
        config, factory_context_,
        [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
           const Ssl::TlsCertificateConfig&,
           absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
    NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
    secret_manager->addCertificateConfig("active-1", nullptr, {});
    EXPECT_OK(secret_manager->updateCertificate("active-1", cert_config));
    secret_manager->addCertificateConfig("pending-1", nullptr, {});
    ASSERT_EQ(2, gaugeValue("on_demand_secret.cert_active"));
    ASSERT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));
  }

  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
}

TEST_F(OnDemandSecretManagerTest, FilterChainUpdateDoesNotLeakSharedGaugesInThreadLocalStore) {
  Stats::SymbolTableImpl symbol_table;
  Stats::Allocator alloc(symbol_table);
  Stats::ThreadLocalStoreImpl store(alloc);
  Stats::ScopeSharedPtr listener_scope = store.rootScope()->createScope("listener.0.0.0.0_443.");

  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(boundedConfig(), config);

  NiceMock<Server::Configuration::MockGenericFactoryContext> fc1_ctx;
  NiceMock<Server::Configuration::MockGenericFactoryContext> fc2_ctx;
  Stats::ScopeSharedPtr fc1_scope = listener_scope->createScope("");
  Stats::ScopeSharedPtr fc2_scope = listener_scope->createScope("");
  ON_CALL(fc1_ctx, scope()).WillByDefault(::testing::ReturnRef(*fc1_scope));
  ON_CALL(fc2_ctx, scope()).WillByDefault(::testing::ReturnRef(*fc2_scope));

  auto make_ctx = [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
                     const Ssl::TlsCertificateConfig&,
                     absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); };

  auto sm1 = std::make_shared<SecretManager>(config, fc1_ctx, make_ctx);
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  sm1->addCertificateConfig("active-1", nullptr, {});
  EXPECT_OK(sm1->updateCertificate("active-1", cert_config));
  sm1->addCertificateConfig("pending-1", nullptr, {});

  // Overlapping LDS filter-chain replacement shares the same underlying listener gauges.
  auto sm2 = std::make_shared<SecretManager>(config, fc2_ctx, make_ctx);
  sm1.reset();
  fc1_scope.reset();

  auto read_store_gauge = [&](absl::string_view full_name) -> std::optional<uint64_t> {
    for (const auto& gauge : store.gauges()) {
      if (gauge->name() == full_name) {
        return gauge->value();
      }
    }
    return std::nullopt;
  };

  auto active_val = read_store_gauge("listener.0.0.0.0_443.on_demand_secret.cert_active");
  auto pending_val = read_store_gauge("listener.0.0.0.0_443.on_demand_secret.cert_pending");
  ASSERT_TRUE(active_val.has_value());
  ASSERT_TRUE(pending_val.has_value());
  EXPECT_EQ(0, *active_val);
  EXPECT_EQ(0, *pending_val);
}

TEST_F(OnDemandSecretManagerTest, CoalescedPendingMissPreservesSecondaryTenantAttribution) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_active_secrets: 4
    max_pending_secrets: 4
  )EOF",
                            config);

  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([](Event::PostCb cb) { cb(); });

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });

  const FairSieve::TenantId flooder_a = FairSieve::computeTenantId("flooder-a");
  const FairSieve::TenantId healthy_b = FairSieve::computeTenantId("healthy-b");

  auto make_handle = [&]() {
    auto cb = std::make_unique<NiceMock<MockCertificateSelectionCallback>>();
    ON_CALL(*cb, dispatcher()).WillByDefault(::testing::ReturnRef(dispatcher));
    return std::make_shared<Handle>(std::move(cb), false);
  };

  // Flooder A starts fetching "shared.com"; healthy tenant B coalesces before SDS responds.
  auto handle_a = make_handle();
  auto handle_b = make_handle();
  secret_manager->addCertificateConfig("shared.com", handle_a, {}, flooder_a);
  secret_manager->addCertificateConfig("shared.com", handle_b, {}, healthy_b);

  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  ASSERT_OK(secret_manager->updateCertificate("shared.com", cert_config));

  // Flooder A scans 4 unique keys; "shared.com" must transfer to B rather than being evicted.
  for (int i = 1; i <= 4; ++i) {
    const std::string noise = absl::StrCat("noise-", i);
    secret_manager->addCertificateConfig(noise, make_handle(), {}, flooder_a);
    ASSERT_OK(secret_manager->updateCertificate(noise, cert_config));
  }

  EXPECT_TRUE(secret_manager->getContext("shared.com").has_value());
  EXPECT_GE(counterValue("on_demand_secret.cert_ownership_transferred"), 1);
}

TEST_F(OnDemandSecretManagerTest, DisconnectedPendingClientsCompactCallbacksAndReclaimSlots) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_pending_secrets: 2
  )EOF",
                            config);

  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([](Event::PostCb cb) { cb(); });
  auto* reclaim_timer = new NiceMock<Event::MockTimer>(&dispatcher);

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  EXPECT_FALSE(reclaim_timer->enabled_);

  auto make_handle = [&]() {
    auto cb = std::make_unique<NiceMock<MockCertificateSelectionCallback>>();
    ON_CALL(*cb, dispatcher()).WillByDefault(::testing::ReturnRef(dispatcher));
    return std::make_shared<Handle>(std::move(cb), false);
  };

  {
    auto h1 = make_handle();
    auto h2 = make_handle();
    secret_manager->addCertificateConfig("bogus-1", h1, {});
    secret_manager->addCertificateConfig("bogus-2", h2, {});
  }
  EXPECT_TRUE(reclaim_timer->enabled_);

  // 50 disconnected retries must compact expired weak_ptrs rather than growing callbacks_ to 51.
  for (int i = 0; i < 50; ++i) {
    auto retry_handle = make_handle();
    secret_manager->addCertificateConfig("bogus-1", retry_handle, {});
  }

  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  bool observed_compacted_callbacks = false;
  {
    LogLevelSetter log_setter(spdlog::level::trace);
    StartStopRecording recorder(GetLogSink());
    EXPECT_OK(secret_manager->updateCertificate("bogus-1", cert_config));
    for (const auto& msg : recorder.messages()) {
      if (absl::StrContains(
              msg, "Notified 0 pending connections about certificate 'bogus-1', out of queued 1")) {
        observed_compacted_callbacks = true;
      }
    }
  }
  EXPECT_TRUE(observed_compacted_callbacks);
  EXPECT_TRUE(reclaim_timer->enabled_);

  // Re-occupy the second slot with a fast-disconnecting waiter. Before `kMinAbandonedPendingAge`
  // elapses, firing the periodic reclaim timer must NOT reclaim abandoned slots so fast disconnects
  // cannot bypass `max_pending_secrets` and amplify SDS load.
  {
    auto h3 = make_handle();
    secret_manager->addCertificateConfig("bogus-3", h3, {});
  }
  simTime().advanceTimeWait(SecretManager::kMinAbandonedPendingAge - std::chrono::seconds(1));
  reclaim_timer->invokeCallback();
  EXPECT_TRUE(reclaim_timer->enabled_);
  EXPECT_EQ(2, gaugeValue("on_demand_secret.cert_pending"));
  {
    auto blocked = make_handle();
    secret_manager->addCertificateConfig("blocked.com", blocked, {});
  }
  EXPECT_EQ(1, counterValue("on_demand_secret.cert_pending_rejected"));
  EXPECT_EQ(2, gaugeValue("on_demand_secret.cert_pending"));

  // Once `kMinAbandonedPendingAge` has elapsed, the periodic reclaim timer sweeps both abandoned
  // pending entries and disarms itself, unblocking subsequent healthy fetches in O(1).
  simTime().advanceTimeWait(std::chrono::seconds(1));
  reclaim_timer->invokeCallback();
  EXPECT_FALSE(reclaim_timer->enabled_);
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_active"));

  auto healthy = make_handle();
  secret_manager->addCertificateConfig("healthy.com", healthy, {});
  EXPECT_EQ(1, counterValue("on_demand_secret.cert_pending_rejected"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_TRUE(reclaim_timer->enabled_);
}

TEST_F(OnDemandSecretManagerTest,
       ReclaimTimerSweepsAbandonedWhilePreservingLiveWaitersAndDisarmsWhenIdle) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_pending_secrets: 4
  )EOF",
                            config);

  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([](Event::PostCb cb) { cb(); });
  auto* reclaim_timer = new NiceMock<Event::MockTimer>(&dispatcher);

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  EXPECT_FALSE(reclaim_timer->enabled_);

  const FairSieve::TenantId tenant_a = FairSieve::computeTenantId("tenant-a");
  const FairSieve::TenantId tenant_b = FairSieve::computeTenantId("tenant-b");
  const FairSieve::TenantId tenant_c = FairSieve::computeTenantId("tenant-c");
  auto make_handle = [&]() {
    auto cb = std::make_unique<NiceMock<MockCertificateSelectionCallback>>();
    ON_CALL(*cb, dispatcher()).WillByDefault(::testing::ReturnRef(dispatcher));
    return std::make_shared<Handle>(std::move(cb), false);
  };

  // Occupy 3/4 pending slots (below saturation): two abandoned entries and one live waiter.
  auto hc_live = make_handle();
  {
    auto ha = make_handle();
    auto hb = make_handle();
    secret_manager->addCertificateConfig("a-abandoned", ha, {}, tenant_a);
    secret_manager->addCertificateConfig("b-abandoned", hb, {}, tenant_b);
    secret_manager->addCertificateConfig("c-live", hc_live, {}, tenant_c);
  }
  ASSERT_EQ(3, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_TRUE(reclaim_timer->enabled_);

  // Periodic timer sweep proactively reclaims abandoned subscriptions even below saturation,
  // preserves entries with live waiters, and stays armed while any pending entry remains.
  simTime().advanceTimeWait(SecretManager::kMinAbandonedPendingAge);
  reclaim_timer->invokeCallback();
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_TRUE(reclaim_timer->enabled_);

  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  ASSERT_OK(secret_manager->updateCertificate("a-abandoned", cert_config));
  EXPECT_FALSE(secret_manager->getContext("a-abandoned").has_value());
  ASSERT_OK(secret_manager->updateCertificate("b-abandoned", cert_config));
  EXPECT_FALSE(secret_manager->getContext("b-abandoned").has_value());

  // Activating the last pending entry disarms the reclaim timer immediately (zero idle wakeups).
  ASSERT_OK(secret_manager->updateCertificate("c-live", cert_config));
  EXPECT_TRUE(secret_manager->getContext("c-live").has_value());
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_FALSE(reclaim_timer->enabled_);

  // Removing the last pending entry via SDS removal also disarms the timer.
  auto ephemeral = make_handle();
  secret_manager->addCertificateConfig("ephemeral-pending", ephemeral, {}, tenant_a);
  EXPECT_TRUE(reclaim_timer->enabled_);
  ASSERT_OK(secret_manager->removeCertificateConfig("ephemeral-pending"));
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_FALSE(reclaim_timer->enabled_);
}

TEST_F(OnDemandSecretManagerTest, RejectsUniquePrefetchExceedingMaxActiveSecrets) {
  const std::string invalid_yaml = R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    prefetch_secret_names: ["p1", "p2", "p3", "p4"]
    max_active_secrets: 2
  )EOF";
  EXPECT_THAT(create(invalid_yaml), StatusIs(absl::StatusCode::kInvalidArgument));

  const std::string duplicate_valid_yaml = R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    prefetch_secret_names: ["p1", "p1", "p2"]
    max_active_secrets: 2
  )EOF";
  EXPECT_OK(create(duplicate_valid_yaml));
}

TEST_F(OnDemandSecretManagerTest, PendingOnlyModeSkipsFilterStateExtractionOnCacheHit) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: sni
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
        default_value: fallback
    max_pending_secrets: 4
  )EOF",
                            config);

  registerFakeProvider("example.com");
  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });

  secret_manager->addCertificateConfig("example.com", nullptr, {});
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  ASSERT_OK(secret_manager->updateCertificate("example.com", cert_config));

  auto& mapper_config = Config::Utility::getAndCheckFactory<Ssl::TlsCertificateMapperConfigFactory>(
      config.certificate_mapper());
  auto msg = Config::Utility::translateAnyToFactoryConfig(
      config.certificate_mapper().typed_config(), factory_context_.messageValidationVisitor(),
      mapper_config);
  auto mapper_factory = mapper_config.createTlsCertificateMapperFactory(*msg, factory_context_);
  ASSERT_OK(mapper_factory);
  AsyncSelector selector((*mapper_factory)(), secret_manager);

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  ASSERT_EQ(1, SSL_set_ex_data(ssl.get(), ContextImpl::sslSocketIndex(), &callbacks));
  auto counting_fs =
      std::make_shared<CountingFilterState>(StreamInfo::FilterState::LifeSpan::Connection);
  counting_fs->setData(TenantIdFilterStateKey,
                       std::make_shared<Router::StringAccessorImpl>("tenant-a"),
                       StreamInfo::FilterState::LifeSpan::Connection);
  callbacks.connection_.stream_info_.filter_state_ = counting_fs;

  SSL_CLIENT_HELLO hello{};
  hello.ssl = ssl.get();

  ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl.get(), "example.com"));
  counting_fs->resetLookups();
  auto hit_res = selector.selectTlsContext(
      hello, std::make_unique<NiceMock<MockCertificateSelectionCallback>>());
  EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Success, hit_res.status);
  EXPECT_EQ(0, counting_fs->readLookups());

  // On a cache miss in pending-only mode, tenant extraction must still run once.
  ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl.get(), "miss.example.com"));
  counting_fs->resetLookups();
  auto miss_res = selector.selectTlsContext(
      hello, std::make_unique<NiceMock<MockCertificateSelectionCallback>>());
  EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Pending, miss_res.status);
  EXPECT_EQ(1, counting_fs->readLookups());

  SSL_set_ex_data(ssl.get(), ContextImpl::sslSocketIndex(), nullptr);
}

TEST_F(OnDemandSecretManagerTest, CertActiveGaugeTracksReadyPlusPending) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(boundedConfig(), config);

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;

  secret_manager->addCertificateConfig("a1", nullptr, {});
  ASSERT_OK(secret_manager->updateCertificate("a1", cert_config));
  secret_manager->addCertificateConfig("a2", nullptr, {});
  ASSERT_OK(secret_manager->updateCertificate("a2", cert_config));
  secret_manager->addCertificateConfig("p1", nullptr, {});
  secret_manager->addCertificateConfig("p2", nullptr, {});

  EXPECT_EQ(2, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_EQ(4, gaugeValue("on_demand_secret.cert_active"));
}

TEST_F(OnDemandSecretManagerTest, IdleTimeoutRemovesUnusedSecretsAndKeepsUsedOnes) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_active_secrets: 8
    idle_timeout: 3600s
  )EOF",
                            config);

  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([](Event::PostCb cb) { cb(); });
  auto* idle_timer = new NiceMock<Event::MockTimer>(&dispatcher);

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  // Nothing loaded yet: no idle wakeups.
  EXPECT_FALSE(idle_timer->enabled_);

  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  for (const char* name : {"used.com", "idle-1.com", "idle-2.com"}) {
    secret_manager->addCertificateConfig(name, nullptr, {});
    ASSERT_OK(secret_manager->updateCertificate(name, cert_config));
  }
  EXPECT_TRUE(idle_timer->enabled_);
  EXPECT_EQ(3, gaugeValue("on_demand_secret.cert_active"));

  // The first sweep consumes the "used since loaded" mark of every secret, the next three count
  // idle intervals; a handshake selects "used.com" before every sweep.
  const auto interval = std::chrono::seconds(3600) / SecretManager::kIdleSweepsPerTimeout;
  for (uint32_t sweep = 1; sweep <= SecretManager::kIdleSweepsPerTimeout; ++sweep) {
    EXPECT_TRUE(secret_manager->getContext("used.com").has_value());
    simTime().advanceTimeWait(interval);
    idle_timer->invokeCallback();
    EXPECT_EQ(3, gaugeValue("on_demand_secret.cert_active")) << "sweep " << sweep;
    EXPECT_TRUE(idle_timer->enabled_);
  }

  // One more interval without a handshake for the idle secrets: both are removed, in one sweep.
  EXPECT_TRUE(secret_manager->getContext("used.com").has_value());
  simTime().advanceTimeWait(interval);
  idle_timer->invokeCallback();
  EXPECT_EQ(2, counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(0, counterValue("on_demand_secret.cert_evicted"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_TRUE(secret_manager->getContext("used.com").has_value());
  EXPECT_FALSE(secret_manager->getContext("idle-1.com").has_value());
  EXPECT_FALSE(secret_manager->getContext("idle-2.com").has_value());
  EXPECT_TRUE(idle_timer->enabled_);

  // A removed name is fetched again on demand like any name that is not cached.
  secret_manager->addCertificateConfig("idle-1.com", nullptr, {});
  ASSERT_OK(secret_manager->updateCertificate("idle-1.com", cert_config));
  EXPECT_TRUE(secret_manager->getContext("idle-1.com").has_value());
  EXPECT_EQ(2, gaugeValue("on_demand_secret.cert_active"));

  // Once no handshake selects anything, every secret goes and the timer disarms. The two
  // lookups above were the last uses; five more sweeps cover both.
  for (int sweep = 0; sweep < 5; ++sweep) {
    simTime().advanceTimeWait(interval);
    idle_timer->invokeCallback();
  }
  EXPECT_EQ(4, counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(0, counterValue("on_demand_secret.cert_evicted"));
  EXPECT_EQ(4, counterValue("on_demand_secret.cert_requested"));
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_FALSE(secret_manager->getContext("used.com").has_value());
  EXPECT_FALSE(secret_manager->getContext("idle-1.com").has_value());
  EXPECT_FALSE(idle_timer->enabled_);
}

TEST_F(OnDemandSecretManagerTest, IdleTimeoutIgnoresPendingSecretsAndCoexistsWithEviction) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_active_secrets: 2
    max_pending_secrets: 2
    idle_timeout: 60s
  )EOF",
                            config);

  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([](Event::PostCb cb) { cb(); });
  // gMock tries the newest expectation first and the manager creates the reclaim timer before
  // the idle timer, so the mocks are declared in the opposite order.
  auto* idle_timer = new NiceMock<Event::MockTimer>(&dispatcher);
  auto* reclaim_timer = new NiceMock<Event::MockTimer>(&dispatcher);

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  auto make_handle = [&]() {
    auto cb = std::make_unique<NiceMock<MockCertificateSelectionCallback>>();
    ON_CALL(*cb, dispatcher()).WillByDefault(::testing::ReturnRef(dispatcher));
    return std::make_shared<Handle>(std::move(cb), false);
  };

  // A waiting handshake keeps "pending.com" pending: that is the reclaim timer's business only.
  auto waiter = make_handle();
  secret_manager->addCertificateConfig("pending.com", waiter, {});
  EXPECT_TRUE(reclaim_timer->enabled_);
  EXPECT_FALSE(idle_timer->enabled_);

  for (const char* name : {"a1", "a2"}) {
    secret_manager->addCertificateConfig(name, nullptr, {});
    ASSERT_OK(secret_manager->updateCertificate(name, cert_config));
  }
  EXPECT_TRUE(idle_timer->enabled_);
  EXPECT_EQ(3, gaugeValue("on_demand_secret.cert_active"));

  const auto interval = std::chrono::seconds(60) / SecretManager::kIdleSweepsPerTimeout;
  for (uint32_t sweep = 0; sweep <= SecretManager::kIdleSweepsPerTimeout; ++sweep) {
    simTime().advanceTimeWait(interval);
    idle_timer->invokeCallback();
  }
  // Both loaded secrets expired. The pending one is untouched and still counted by both gauges.
  EXPECT_EQ(2, counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_FALSE(secret_manager->getContext("a1").has_value());
  EXPECT_FALSE(secret_manager->getContext("a2").has_value());
  // Nothing is loaded, so the idle timer is disarmed even though a fetch is still pending.
  EXPECT_FALSE(idle_timer->enabled_);
  EXPECT_TRUE(reclaim_timer->enabled_);

  // Delivery of the pending secret arms the idle timer again.
  ASSERT_OK(secret_manager->updateCertificate("pending.com", cert_config));
  EXPECT_TRUE(idle_timer->enabled_);
  EXPECT_FALSE(reclaim_timer->enabled_);

  // Eviction for space keeps working next to idle expiry and is counted separately.
  for (const char* name : {"b1", "b2"}) {
    secret_manager->addCertificateConfig(name, nullptr, {});
    ASSERT_OK(secret_manager->updateCertificate(name, cert_config));
  }
  EXPECT_EQ(1, counterValue("on_demand_secret.cert_evicted"));
  EXPECT_EQ(2, counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(2, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
}

TEST_F(OnDemandSecretManagerTest, IdleTimeoutConfigValidation) {
  const auto yaml = [](absl::string_view extra) {
    return absl::StrCat(R"EOF(
config_source:
  ads: {}
certificate_mapper:
  name: static-name
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
    name: server
)EOF",
                        extra);
  };
  EXPECT_OK(create(yaml("max_active_secrets: 100\nidle_timeout: 21600s\n")));
  // Hits are only recorded when active eviction is enabled, so the timeout needs the cap.
  EXPECT_THAT(create(yaml("idle_timeout: 21600s\n")), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(create(yaml("max_pending_secrets: 16\nidle_timeout: 21600s\n")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THROW_WITH_REGEX(
      create(yaml("max_active_secrets: 100\nidle_timeout: 0.5s\n")).IgnoreError(), EnvoyException,
      "IdleTimeout");
}

TEST_F(OnDemandSecretManagerTest,
       IdleTimeoutExemptsPrefetchedSecretsAndDisarmsWhenOnlyPrefetchedRemain) {
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    prefetch_secret_names: ["prefetch.com"]
    max_active_secrets: 4
    idle_timeout: 60s
  )EOF",
                            config);

  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([](Event::PostCb cb) { cb(); });
  auto* idle_timer = new NiceMock<Event::MockTimer>(&dispatcher);

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;

  // Activating only the prefetched secret must NOT arm the idle timer (zero idle wakeups).
  ASSERT_OK(secret_manager->updateCertificate("prefetch.com", cert_config));
  EXPECT_TRUE(secret_manager->getContext("prefetch.com").has_value());
  EXPECT_FALSE(idle_timer->enabled_);

  // Adding a non-prefetched on-demand secret arms the idle timer.
  secret_manager->addCertificateConfig("dynamic.com", nullptr, {});
  ASSERT_OK(secret_manager->updateCertificate("dynamic.com", cert_config));
  EXPECT_TRUE(idle_timer->enabled_);
  EXPECT_EQ(2, gaugeValue("on_demand_secret.cert_active"));

  // After full idle timeout without hits, only "dynamic.com" expires; "prefetch.com" remains
  // cached and the idle timer disarms because no non-prefetched secret remains.
  const auto interval = std::chrono::seconds(60) / SecretManager::kIdleSweepsPerTimeout;
  for (uint32_t sweep = 0; sweep <= SecretManager::kIdleSweepsPerTimeout; ++sweep) {
    simTime().advanceTimeWait(interval);
    idle_timer->invokeCallback();
  }
  EXPECT_EQ(1, counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_TRUE(secret_manager->getContext("prefetch.com").has_value());
  EXPECT_FALSE(secret_manager->getContext("dynamic.com").has_value());
  EXPECT_FALSE(idle_timer->enabled_);

  // Removing the last non-prefetched secret via SDS also disarms the idle timer immediately.
  secret_manager->addCertificateConfig("ephemeral.com", nullptr, {});
  ASSERT_OK(secret_manager->updateCertificate("ephemeral.com", cert_config));
  EXPECT_TRUE(idle_timer->enabled_);
  ASSERT_OK(secret_manager->removeCertificateConfig("ephemeral.com"));
  EXPECT_FALSE(idle_timer->enabled_);
  EXPECT_TRUE(secret_manager->getContext("prefetch.com").has_value());
}

TEST_F(OnDemandSecretManagerTest, IdleTimeoutPacesLargeSweepsAndSkipsReusedOrSupersededSecrets) {
  constexpr size_t kTotalSecrets = SecretManager::kMaxIdleRemovalsPerTick + 4;
  envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config config;
  TestUtility::loadFromYaml(absl::StrCat(R"EOF(
    config_source:
      ads: {}
    certificate_mapper:
      name: static-name
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
        name: server
    max_active_secrets: )EOF",
                                         kTotalSecrets + 8, "\n    idle_timeout: 400s\n"),
                            config);

  auto& dispatcher = factory_context_.server_context_.dispatcher_;
  EXPECT_CALL(dispatcher, post(_)).WillRepeatedly([](Event::PostCb cb) { cb(); });
  auto* idle_timer = new NiceMock<Event::MockTimer>(&dispatcher);
  std::chrono::milliseconds last_armed_ms{-1};
  ON_CALL(*idle_timer, enableTimer(_, _))
      .WillByDefault([&](const std::chrono::milliseconds& ms, const ScopeTrackedObject*) {
        idle_timer->enabled_ = true;
        last_armed_ms = ms;
      });

  auto secret_manager = std::make_shared<SecretManager>(
      config, factory_context_,
      [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) { return std::make_shared<FakeAsyncContext>(scope); });
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;

  std::vector<std::string> all_names;
  all_names.reserve(kTotalSecrets);
  for (size_t i = 0; i < kTotalSecrets; ++i) {
    all_names.push_back(absl::StrCat("s-", i, ".example.com"));
    secret_manager->addCertificateConfig(all_names.back(), nullptr, {});
    ASSERT_OK(secret_manager->updateCertificate(all_names.back(), cert_config));
  }
  const auto sweep_interval =
      std::chrono::milliseconds(400000 / SecretManager::kIdleSweepsPerTimeout);
  EXPECT_EQ(sweep_interval, last_armed_ms);
  ASSERT_EQ(kTotalSecrets, gaugeValue("on_demand_secret.cert_active"));

  // First `kIdleSweepsPerTimeout` sweeps mark all entries idle up to one interval shy of expiry.
  for (uint32_t sweep = 1; sweep <= SecretManager::kIdleSweepsPerTimeout; ++sweep) {
    simTime().advanceTimeWait(sweep_interval);
    idle_timer->invokeCallback();
    EXPECT_EQ(kTotalSecrets, gaugeValue("on_demand_secret.cert_active"));
    EXPECT_EQ(sweep_interval, last_armed_ms);
  }

  // Expiry sweep tick 1: removes at most `kMaxIdleRemovalsPerTick` and yields via a 0 ms timer.
  simTime().advanceTimeWait(sweep_interval);
  idle_timer->invokeCallback();
  EXPECT_EQ(SecretManager::kMaxIdleRemovalsPerTick,
            counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(4, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_TRUE(idle_timer->enabled_);
  EXPECT_EQ(std::chrono::milliseconds(0), last_armed_ms);

  // Hitting the 4 queued secrets before tick 2 must spare all of them from expiration and
  // re-arm the timer at `sweep_interval` once the drain completes.
  std::vector<std::string> remaining;
  for (const std::string& name : all_names) {
    if (secret_manager->getContext(name).has_value()) {
      remaining.push_back(name);
    }
  }
  ASSERT_EQ(4, remaining.size());
  idle_timer->invokeCallback();
  EXPECT_EQ(SecretManager::kMaxIdleRemovalsPerTick,
            counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(4, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_EQ(sweep_interval, last_armed_ms);

  // Now verify ABA safety + partial drain without touching the remaining secrets: load another
  // batch of `kMaxIdleRemovalsPerTick` secrets so `kTotalSecrets` are active again, expire them,
  // remove+re-add one secret between tick 1 and tick 2, and confirm tick 2 spares the new
  // generation while expiring the untouched entries.
  for (size_t i = 0; i < SecretManager::kMaxIdleRemovalsPerTick; ++i) {
    const std::string name = absl::StrCat("b-", i, ".example.com");
    secret_manager->addCertificateConfig(name, nullptr, {});
    ASSERT_OK(secret_manager->updateCertificate(name, cert_config));
  }
  ASSERT_EQ(kTotalSecrets, gaugeValue("on_demand_secret.cert_active"));
  for (uint32_t sweep = 1; sweep <= SecretManager::kIdleSweepsPerTimeout; ++sweep) {
    simTime().advanceTimeWait(sweep_interval);
    idle_timer->invokeCallback();
  }
  ASSERT_EQ(kTotalSecrets, gaugeValue("on_demand_secret.cert_active"));

  // Tick 1 drains `kMaxIdleRemovalsPerTick` entries, leaving 4 queued (without touching them).
  const uint64_t expired_before = counterValue("on_demand_secret.cert_idle_expired");
  simTime().advanceTimeWait(sweep_interval);
  idle_timer->invokeCallback();
  EXPECT_EQ(expired_before + SecretManager::kMaxIdleRemovalsPerTick,
            counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(4, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_EQ(std::chrono::milliseconds(0), last_armed_ms);

  // ABA check between paced ticks: find one of the 4 queued secrets via SDS removal (without
  // hitting the other 3) and immediately start a fresh fetch for the same name (new generation).
  std::string aba_name;
  for (size_t i = 0; i < SecretManager::kMaxIdleRemovalsPerTick && aba_name.empty(); ++i) {
    const std::string candidate = absl::StrCat("b-", i, ".example.com");
    ASSERT_OK(secret_manager->removeCertificateConfig(candidate));
    if (gaugeValue("on_demand_secret.cert_active") == 3) {
      aba_name = candidate;
    }
  }
  for (const std::string& candidate : remaining) {
    if (!aba_name.empty()) {
      break;
    }
    ASSERT_OK(secret_manager->removeCertificateConfig(candidate));
    if (gaugeValue("on_demand_secret.cert_active") == 3) {
      aba_name = candidate;
    }
  }
  ASSERT_FALSE(aba_name.empty());
  secret_manager->addCertificateConfig(aba_name, nullptr, {});
  ASSERT_EQ(4, gaugeValue("on_demand_secret.cert_active"));
  ASSERT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));

  // Tick 2 expires only the 3 untouched queued secrets, spares the superseded pending generation,
  // and disarms the idle timer (no active non-prefetched secrets remain).
  idle_timer->invokeCallback();
  EXPECT_EQ(expired_before + kTotalSecrets - 1, counterValue("on_demand_secret.cert_idle_expired"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_active"));
  EXPECT_EQ(1, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_FALSE(idle_timer->enabled_);

  // Delivering the re-fetched secret activates the new generation and re-arms the idle timer.
  ASSERT_OK(secret_manager->updateCertificate(aba_name, cert_config));
  EXPECT_TRUE(secret_manager->getContext(aba_name).has_value());
  EXPECT_EQ(0, gaugeValue("on_demand_secret.cert_pending"));
  EXPECT_TRUE(idle_timer->enabled_);
  EXPECT_EQ(sweep_interval, last_armed_ms);
}

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
