#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"

#include "source/common/config/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/ocsp/ocsp.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
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

// The ClientHello extensions of a client that requests an OCSP response.
constexpr uint8_t StatusRequestExtension[] = {0x00, 0x05, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00};

class ServerNameMapper : public Ssl::TlsCertificateMapper {
public:
  std::string deriveFromClientHello(const SSL_CLIENT_HELLO&) override { return "server"; }
};

// A certificate context with an OCSP response under an OCSP staple policy, whose certificate has
// the must-staple extension when `must_staple` is set.
class TestAsyncContext : public AsyncContext {
public:
  TestAsyncContext(Stats::Scope& scope, Ssl::ServerContextConfig::OcspStaplePolicy policy,
                   std::unique_ptr<Ssl::OcspResponseWrapper> response, bool must_staple)
      : AsyncContext(scope), policy_(policy) {
    tls_context_.ocsp_response_ = std::move(response);
    tls_context_.is_must_staple_ = must_staple;
  }

  Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const override { return policy_; }
  const Ssl::TlsContext& tlsContext() const override { return tls_context_; }

private:
  const Ssl::ServerContextConfig::OcspStaplePolicy policy_;
  Ssl::TlsContext tls_context_;
};

// A selection callback that ignores the result it receives.
class TestSelectionCallback : public Ssl::CertificateSelectionCallback {
public:
  explicit TestSelectionCallback(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void onCertificateSelectionResult(OptRef<const Ssl::TlsContext>, bool) override {}

private:
  Event::Dispatcher& dispatcher_;
};

class CachedCertificateSelectionTest : public testing::Test {
protected:
  // Caches a "server" context that carries the OCSP response `ocsp_staple` under `policy`.
  void cacheContext(Ssl::ServerContextConfig::OcspStaplePolicy policy,
                    absl::string_view ocsp_staple, bool must_staple = false) {
    ASSERT_NO_FATAL_FAILURE(makeContext(policy, ocsp_staple, must_staple));
    secret_manager_->setContext("server", context_);
  }

  // Builds the "server" context without caching it.
  void makeContext(Ssl::ServerContextConfig::OcspStaplePolicy policy, absl::string_view ocsp_staple,
                   bool must_staple = false) {
    const std::string der = TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath(absl::StrCat("test/config/integration/certs/", ocsp_staple)));
    auto response =
        Ocsp::OcspResponseWrapperImpl::create(std::vector<uint8_t>(der.begin(), der.end()),
                                              factory_context_.serverFactoryContext().timeSource());
    ASSERT_OK(response);
    context_ = std::make_shared<TestAsyncContext>(factory_context_.scope(), policy,
                                                  *std::move(response), must_staple);
  }

  // Selects the "server" context for a client that requests an OCSP response when `request_ocsp`.
  Ssl::SelectionResult select(bool request_ocsp,
                              Ssl::CertificateSelectionCallbackPtr cb = nullptr) {
    AsyncSelector selector(std::make_unique<ServerNameMapper>(), secret_manager_);
    SSL_CLIENT_HELLO client_hello{};
    client_hello.ssl = ssl_.get();
    if (request_ocsp) {
      client_hello.extensions = StatusRequestExtension;
      client_hello.extensions_len = sizeof(StatusRequestExtension);
    }
    return selector.selectTlsContext(client_hello, std::move(cb));
  }

  // Drops the context from the cache and from the test, and expects `handle` alone to keep it.
  void expectHandleKeepsContext(Ssl::SelectionHandleConstSharedPtr handle) {
    const std::weak_ptr<const AsyncContext> weak_context = context_;
    secret_manager_->setContext("server", nullptr);
    context_.reset();
    EXPECT_FALSE(weak_context.expired());
    handle.reset();
    EXPECT_TRUE(weak_context.expired());
  }

  uint64_t ocspStapleFailed() {
    return factory_context_.statsScope().counterFromString("ssl.ocsp_staple_failed").value();
  }

  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context_;
  std::shared_ptr<SecretManager> secret_manager_ = std::make_shared<SecretManager>(
      ConfigProto(), factory_context_,
      [](Stats::Scope&, Server::Configuration::ServerFactoryContext&,
         const Ssl::TlsCertificateConfig&,
         absl::Status&) -> AsyncContextSharedPtr { return nullptr; });
  std::shared_ptr<TestAsyncContext> context_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_{SSL_CTX_new(TLS_method())};
  bssl::UniquePtr<SSL> ssl_{SSL_new(ssl_ctx_.get())};
};

// A certificate whose OCSP response expired fails the selection under MUST_STAPLE for a client that
// requests the response, and the failure counts in `ssl.ocsp_staple_failed` and names the secret.
TEST_F(CachedCertificateSelectionTest, FailsExpiredOcspResponse) {
  ASSERT_NO_FATAL_FAILURE(cacheContext(Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple,
                                       "server_expired_ocsp_resp.der"));
  EXPECT_LOG_CONTAINS("debug", "TLS certificate 'server' fails the OCSP policy", {
    const Ssl::SelectionResult result = select(true);
    EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Failed, result.status);
    EXPECT_EQ(nullptr, result.selected_ctx);
  });
  EXPECT_EQ(1, ocspStapleFailed());
}

// The must-staple extension of a certificate upgrades LENIENT_STAPLING to MUST_STAPLE, so an
// expired OCSP response fails the selection.
TEST_F(CachedCertificateSelectionTest, FailsMustStapleCertificateUnderLenientStapling) {
  ASSERT_NO_FATAL_FAILURE(cacheContext(Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling,
                                       "server_expired_ocsp_resp.der", true));
  EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Failed, select(true).status);
  EXPECT_EQ(1, ocspStapleFailed());
}

// The failure line of a cached selection carries the ConnectionId tag of the handshake's
// connection.
TEST_F(CachedCertificateSelectionTest, LogsConnectionOfFailure) {
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  ASSERT_EQ(1, SSL_set_ex_data(ssl_.get(), ContextImpl::sslSocketIndex(), &callbacks));
  ASSERT_NO_FATAL_FAILURE(cacheContext(Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple,
                                       "server_expired_ocsp_resp.der"));
  EXPECT_LOG_CONTAINS(
      "debug",
      absl::StrCat("\"ConnectionId\":\"", callbacks.connection_.id(),
                   "\"] TLS certificate 'server' fails the OCSP policy"),
      { EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Failed, select(true).status); });
}

// The failure line of a pending selection carries the ConnectionId tag that the handle keeps from
// the worker, since the selection completes on the main thread.
TEST_F(CachedCertificateSelectionTest, LogsConnectionOfPendingFailure) {
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  ASSERT_EQ(1, SSL_set_ex_data(ssl_.get(), ContextImpl::sslSocketIndex(), &callbacks));
  ASSERT_NO_FATAL_FAILURE(makeContext(Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple,
                                      "server_expired_ocsp_resp.der"));
  // The test notifies the handle itself, so the subscription the selection posts is not needed.
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_));
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  const Ssl::SelectionResult result =
      select(true, std::make_unique<TestSelectionCallback>(worker_dispatcher));
  ASSERT_EQ(Ssl::SelectionResult::SelectionStatus::Pending, result.status);
  const auto handle =
      std::const_pointer_cast<Handle>(std::dynamic_pointer_cast<const Handle>(result.handle));
  ASSERT_NE(nullptr, handle);
  EXPECT_LOG_CONTAINS("debug",
                      absl::StrCat("\"ConnectionId\":\"", callbacks.connection_.id(),
                                   "\"] TLS certificate 'server' fails the OCSP policy"),
                      handle->notify(context_));
  EXPECT_EQ(1, ocspStapleFailed());
}

// A client that requests no OCSP response gets the same certificate without a staple, and the
// handle alone keeps its context alive once the cache drops it.
TEST_F(CachedCertificateSelectionTest, KeepsContextWithoutStaple) {
  ASSERT_NO_FATAL_FAILURE(cacheContext(Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple,
                                       "server_expired_ocsp_resp.der"));
  Ssl::SelectionResult result = select(false);
  EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Success, result.status);
  EXPECT_EQ(&context_->tlsContext(), result.selected_ctx);
  EXPECT_FALSE(result.staple);
  expectHandleKeepsContext(std::move(result.handle));
  EXPECT_EQ(0, ocspStapleFailed());
}

// Runs with the runtime guard on and off.
class CachedCertificateSelectionGuardTest : public CachedCertificateSelectionTest,
                                            public testing::WithParamInterface<bool> {};

INSTANTIATE_TEST_SUITE_P(RuntimeGuard, CachedCertificateSelectionGuardTest, testing::Bool());

// A selection from the cache staples a valid OCSP response, and its handle alone keeps the
// certificate's context alive once the cache drops it.
TEST_P(CachedCertificateSelectionGuardTest, StaplesAndKeepsContext) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.on_demand_secret_enforce_ocsp_staple_policy",
        GetParam() ? "true" : "false"}});
  ASSERT_NO_FATAL_FAILURE(
      cacheContext(Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple, "server_ocsp_resp.der"));
  Ssl::SelectionResult result = select(true);
  EXPECT_EQ(Ssl::SelectionResult::SelectionStatus::Success, result.status);
  EXPECT_EQ(&context_->tlsContext(), result.selected_ctx);
  EXPECT_TRUE(result.staple);
  expectHandleKeepsContext(std::move(result.handle));
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
