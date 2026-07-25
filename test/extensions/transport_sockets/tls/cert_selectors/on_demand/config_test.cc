#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"

#include "source/common/common/callback_impl.h"
#include "source/common/config/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/logging.h"
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
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

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

TEST_F(OnDemandTest, MaxSecretsValid) {
  EXPECT_OK(create(R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      max_secrets: 1
      cache_idle_timeout: 300s
    )EOF"));
}

TEST_F(OnDemandTest, MaxSecretsPrefetchOverflow) {
  auto factory = create(R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      prefetch_secret_names:
      - server
      - server2
      max_secrets: 1
    )EOF");
  EXPECT_THAT(factory, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(factory.status().message(),
              testing::HasSubstr("The number of prefetched secrets (2) exceeds the maximum number "
                                 "of cached secrets (1)."));
}

TEST_F(OnDemandTest, QuicCall) {
  auto factory = create(defaultConfig());
  EXPECT_OK(factory);
  auto selector = factory.value()->create(selector_context_);
  bool sni;
  absl::InlinedVector<int, 3> curve;
  EXPECT_DEATH(selector->findTlsContext("", curve, false, &sni), "Not supported with QUIC");
}

TEST_F(OnDemandTest, CacheIdleTimeoutTooSmall) {
  for (const std::string timeout : {"0.000000001s", "0.000999s"}) {
    const std::string config =
        absl::StrCat(defaultConfig(), "\n      cache_idle_timeout: ", timeout);
    EXPECT_THROW_WITH_REGEX(
        { auto result = create(config); }, EnvoyException, "cache_idle_timeout");
  }
}

TEST_F(OnDemandTest, CacheIdleTimeoutMinimumAccepted) {
  EXPECT_OK(create(absl::StrCat(defaultConfig(), "\n      cache_idle_timeout: 0.001s")));
}

// Deterministic tests for the cache limit and idle eviction semantics of the SecretManager,
// driving the sweep timer manually.
class SecretManagerTest : public ::testing::Test {
protected:
  // An inert secret provider: the secret never resolves on its own, and callbacks are registered
  // but only fired by the test.
  class FakeProvider : public Secret::TlsCertificateConfigProvider {
  public:
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate* secret() const override {
      return nullptr;
    }
    Envoy::Common::CallbackHandlePtr addValidationCallback(
        std::function<absl::Status(
            const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)>) override {
      return nullptr;
    }
    Envoy::Common::CallbackHandlePtr
    addUpdateCallback(std::function<absl::Status()> callback) override {
      return update_cbs_.add(callback);
    }
    Envoy::Common::CallbackHandlePtr
    addRemoveCallback(std::function<absl::Status()> callback) override {
      return remove_cbs_.add(callback);
    }
    void start() override {}

  private:
    Envoy::Common::CallbackManager<absl::Status> update_cbs_;
    Envoy::Common::CallbackManager<absl::Status> remove_cbs_;
  };

  // A certificate context that skips real TLS setup; the tests never complete a handshake.
  class TestAsyncContext : public AsyncContext {
  public:
    explicit TestAsyncContext(Stats::Scope& scope) : AsyncContext(scope) {}
    Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const override {
      return Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling;
    }
    const Ssl::TlsContext& tlsContext() const override { PANIC("not used in tests"); }
  };

  std::shared_ptr<SecretManager> makeManager(const std::string& config_yaml) {
    ON_CALL(factory_context_.server_context_, secretManager())
        .WillByDefault(ReturnRef(secret_manager_mock_));
    ON_CALL(secret_manager_mock_, findOrCreateTlsCertificateProvider(_, _, _, _, _))
        .WillByDefault(Return(provider_));
    ConfigProto config;
    TestUtility::loadFromYaml(config_yaml, config);
    if (config.has_cache_idle_timeout()) {
      // Ownership passes to the SecretManager via createTimer().
      sweep_timer_ = new NiceMock<Event::MockTimer>(&factory_context_.server_context_.dispatcher_);
    }
    return std::make_shared<SecretManager>(
        config, factory_context_,
        [](Stats::Scope& scope, Server::Configuration::ServerFactoryContext&,
           const Ssl::TlsCertificateConfig&,
           absl::Status&) -> AsyncContextConstSharedPtr { // NOLINT
          return std::make_shared<TestAsyncContext>(scope);
        });
  }

  void sweep() { sweep_timer_->invokeCallback(); }

  uint64_t counter(absl::string_view name) {
    return factory_context_.store_.counterFromString(absl::StrCat("on_demand_secret.", name))
        .value();
  }

  int64_t activeGauge() {
    return factory_context_.store_
        .gaugeFromString("on_demand_secret.cert_active", Stats::Gauge::ImportMode::Accumulate)
        .value();
  }

  static constexpr absl::string_view kIdleConfig = R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      cache_idle_timeout: 60s
    )EOF";

  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context_;
  NiceMock<Secret::MockSecretManager> secret_manager_mock_;
  std::shared_ptr<FakeProvider> provider_{std::make_shared<FakeProvider>()};
  NiceMock<Event::MockTimer>* sweep_timer_{};
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config_;
};

// A never-resolved, never-used entry survives exactly one sweep: the first sweep consumes the
// creation grace and the second evicts, i.e. eviction within (1, 2] idle timeouts.
TEST_F(SecretManagerTest, IdleSweepTiming) {
  auto manager = makeManager(std::string(kIdleConfig));
  manager->addCertificateConfig("a", nullptr, {});
  EXPECT_EQ(1, activeGauge());
  sweep();
  EXPECT_EQ(0, counter("cert_evicted"));
  EXPECT_EQ(1, activeGauge());
  sweep();
  EXPECT_EQ(1, counter("cert_evicted"));
  EXPECT_EQ(0, activeGauge());
}

// SDS certificate updates do not count as handshake activity: a secret that is rotated but never
// used by a handshake is still evicted on the second sweep.
TEST_F(SecretManagerTest, SdsUpdateIsNotActivity) {
  auto manager = makeManager(std::string(kIdleConfig));
  manager->addCertificateConfig("a", nullptr, {});
  EXPECT_OK(manager->updateCertificate("a", cert_config_));
  sweep();
  // Rotate the certificate between sweeps.
  EXPECT_OK(manager->updateCertificate("a", cert_config_));
  EXPECT_OK(manager->updateCertificate("a", cert_config_));
  sweep();
  EXPECT_EQ(1, counter("cert_evicted"));
  EXPECT_EQ(0, activeGauge());
}

// A handshake between sweeps resets the idle time by exactly one period.
TEST_F(SecretManagerTest, HandshakeExtendsLifetime) {
  auto manager = makeManager(std::string(kIdleConfig));
  manager->addCertificateConfig("a", nullptr, {});
  EXPECT_OK(manager->updateCertificate("a", cert_config_));
  sweep();
  // Simulate a worker-thread handshake using the cached certificate.
  EXPECT_TRUE(manager->getContext("a").has_value());
  sweep();
  EXPECT_EQ(0, counter("cert_evicted"));
  sweep();
  EXPECT_EQ(1, counter("cert_evicted"));
}

// An entry with a live pending handshake is never evicted; once the handshake goes away the
// entry becomes idle and is evicted.
TEST_F(SecretManagerTest, PendingHandshakeProtected) {
  auto manager = makeManager(std::string(kIdleConfig));
  auto handle = std::make_shared<Handle>(AsyncContextConstSharedPtr(nullptr));
  manager->addCertificateConfig("a", handle, {});
  sweep();
  sweep();
  sweep();
  EXPECT_EQ(0, counter("cert_evicted"));
  EXPECT_EQ(1, activeGauge());
  handle.reset();
  sweep();
  EXPECT_EQ(1, counter("cert_evicted"));
  EXPECT_EQ(0, activeGauge());
}

// Prefetched secrets are pinned even after the SDS server removes them and a handshake fetches
// them again on-demand.
TEST_F(SecretManagerTest, PrefetchPinnedAcrossRefetch) {
  auto manager = makeManager(absl::StrCat(std::string(kIdleConfig), R"EOF(
      prefetch_secret_names:
      - pinned
    )EOF"));
  EXPECT_EQ(1, activeGauge());
  sweep();
  sweep();
  sweep();
  EXPECT_EQ(0, counter("cert_evicted"));

  // The SDS server removes the resource; the posted removal runs inline on the mock dispatcher.
  EXPECT_OK(manager->removeCertificateConfig("pinned"));
  EXPECT_EQ(0, activeGauge());

  // A later handshake fetches the same name on-demand: it must be pinned again.
  manager->addCertificateConfig("pinned", nullptr, {});
  sweep();
  sweep();
  sweep();
  EXPECT_EQ(0, counter("cert_evicted"));
  EXPECT_EQ(1, activeGauge());
}

// Expired pending handshake handles are compacted at geometric size thresholds so the callback
// list stays bounded by roughly twice the live handshakes, not by the history of interrupted
// ones, while insertion cost stays amortized constant.
TEST_F(SecretManagerTest, ExpiredCallbacksCompacted) {
  auto manager = makeManager(std::string(kIdleConfig));
  for (int i = 0; i < 100; i++) {
    auto handle = std::make_shared<Handle>(AsyncContextConstSharedPtr(nullptr));
    manager->addCertificateConfig("a", handle, {});
    // The handle goes out of scope, simulating an interrupted handshake.
    EXPECT_LE(manager->pendingCallbacksForTest("a"), 16);
  }
  sweep();
  EXPECT_EQ(0, manager->pendingCallbacksForTest("a"));
}

// When the cache is full, a pending entry with no certificate and no live handshakes is
// reclaimed to admit a new secret; entries with certificates or live handshakes are not, and
// the new secret is rejected instead.
TEST_F(SecretManagerTest, ReclaimPendingEntryOverRejection) {
  auto manager = makeManager(R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      max_secrets: 2
    )EOF");
  // "abandoned" has no certificate and no live handshake: reclaimable.
  manager->addCertificateConfig("abandoned", nullptr, {});
  // "resolved" has a certificate: not reclaimable.
  manager->addCertificateConfig("resolved", nullptr, {});
  EXPECT_OK(manager->updateCertificate("resolved", cert_config_));

  // Admitting a new secret reclaims the abandoned entry instead of rejecting.
  manager->addCertificateConfig("incoming", nullptr, {});
  EXPECT_EQ(1, counter("cert_reclaimed"));
  EXPECT_EQ(0, counter("cert_overflow"));
  EXPECT_EQ(2, activeGauge());

  // "incoming" now has a live pending handshake: protected from reclaim, so the next new secret
  // is rejected.
  auto handle = std::make_shared<Handle>(AsyncContextConstSharedPtr(nullptr));
  manager->addCertificateConfig("incoming", handle, {});
  manager->addCertificateConfig("rejected", nullptr, {});
  EXPECT_EQ(1, counter("cert_reclaimed"));
  EXPECT_EQ(1, counter("cert_overflow"));
  EXPECT_EQ(2, activeGauge());
}

// The callback list stays bounded by roughly twice the live pending handshakes even when
// interrupted handshakes are interleaved, and live handles are never dropped by compaction.
TEST_F(SecretManagerTest, CompactionThresholdGrowsWithLiveHandshakes) {
  auto manager = makeManager(std::string(kIdleConfig));
  std::vector<HandleSharedPtr> live;
  for (int i = 0; i < 20; i++) {
    live.push_back(std::make_shared<Handle>(AsyncContextConstSharedPtr(nullptr)));
    manager->addCertificateConfig("a", live.back(), {});
  }
  for (int i = 0; i < 100; i++) {
    auto handle = std::make_shared<Handle>(AsyncContextConstSharedPtr(nullptr));
    manager->addCertificateConfig("a", handle, {});
    // With 20 live handles the compaction threshold doubles to 40, which bounds the list.
    EXPECT_LE(manager->pendingCallbacksForTest("a"), 40);
  }
  EXPECT_GE(manager->pendingCallbacksForTest("a"), 20);
  live.clear();
  sweep();
  EXPECT_EQ(0, manager->pendingCallbacksForTest("a"));
}

// A configured cache limit without an idle timeout cannot release resolved secrets, which is
// worth a warning; configuring both is silent.
TEST_F(SecretManagerTest, WarnsWhenLimitSetWithoutIdleTimeout) {
  EXPECT_LOG_CONTAINS("warning", "max_secrets is configured without cache_idle_timeout", {
    auto manager = makeManager(R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      max_secrets: 1
    )EOF");
  });
  EXPECT_LOG_NOT_CONTAINS("warning", "max_secrets is configured without cache_idle_timeout", {
    auto manager = makeManager(absl::StrCat(std::string(kIdleConfig), R"EOF(
      max_secrets: 1
    )EOF"));
  });
}

// A deferred SDS removal does not erase a certificate that the SDS server published after
// signaling the removal: the later message wins.
TEST_F(SecretManagerTest, StaleRemovalIgnoredAfterUpdate) {
  Event::PostCb stale_removal;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillOnce([&](Event::PostCb callback) { stale_removal = std::move(callback); })
      .WillRepeatedly([](Event::PostCb callback) { callback(); });
  auto manager = makeManager(R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF");
  manager->addCertificateConfig("x", nullptr, {});
  EXPECT_OK(manager->updateCertificate("x", cert_config_));
  // The SDS server removes "x"; the removal is deferred to the main dispatcher.
  EXPECT_OK(manager->removeCertificateConfig("x"));
  // Before the deferred removal runs, the SDS server publishes a newer certificate.
  EXPECT_OK(manager->updateCertificate("x", cert_config_));
  // The stale removal must keep the newer certificate installed.
  std::move(stale_removal)();
  EXPECT_EQ(1, activeGauge());
  EXPECT_TRUE(manager->getContext("x").has_value());
}

// A single sweep evicts multiple idle entries, erasing the thread local contexts of the resolved
// ones so that later lookups miss.
TEST_F(SecretManagerTest, BatchEvictionRemovesResolvedContexts) {
  auto manager = makeManager(std::string(kIdleConfig));
  manager->addCertificateConfig("a", nullptr, {});
  manager->addCertificateConfig("b", nullptr, {});
  manager->addCertificateConfig("c", nullptr, {});
  EXPECT_OK(manager->updateCertificate("c", cert_config_));
  EXPECT_TRUE(manager->getContext("c").has_value());
  sweep();
  sweep();
  EXPECT_EQ(3, counter("cert_evicted"));
  EXPECT_EQ(0, activeGauge());
  EXPECT_FALSE(manager->getContext("c").has_value());
}

// A deferred SDS removal does not erase an entry that was reclaimed and re-created for the same
// name while the removal was in flight.
TEST_F(SecretManagerTest, StaleRemovalIgnoredAfterReadmission) {
  Event::PostCb stale_removal;
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_))
      .WillOnce([&](Event::PostCb callback) { stale_removal = std::move(callback); })
      .WillRepeatedly([](Event::PostCb callback) { callback(); });
  auto manager = makeManager(R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      max_secrets: 1
    )EOF");
  manager->addCertificateConfig("x", nullptr, {});
  // The SDS server removes "x"; the removal is deferred to the main dispatcher.
  EXPECT_OK(manager->removeCertificateConfig("x"));
  // Before the deferred removal runs, the slot is reclaimed and the name is fetched again.
  manager->addCertificateConfig("y", nullptr, {});
  manager->addCertificateConfig("x", nullptr, {});
  EXPECT_EQ(2, counter("cert_reclaimed"));
  EXPECT_EQ(1, activeGauge());
  // The stale removal must not erase the new incarnation of "x".
  std::move(stale_removal)();
  EXPECT_EQ(1, activeGauge());
}

// When max_secrets is unset the cache is unlimited and admission never reclaims or rejects.
TEST_F(SecretManagerTest, UnsetLimitIsUnlimited) {
  auto manager = makeManager(R"EOF(
      config_source:
        ads: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
    )EOF");
  for (int i = 0; i < 1500; i++) {
    manager->addCertificateConfig(absl::StrCat("secret_", i), nullptr, {});
  }
  EXPECT_EQ(1500, activeGauge());
  EXPECT_EQ(0, counter("cert_reclaimed"));
  EXPECT_EQ(0, counter("cert_overflow"));
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
