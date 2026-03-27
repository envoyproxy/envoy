#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3/config.pb.h"

#include "source/common/config/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/filter_state/config.h"

#include "test/mocks/network/connection.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace FilterState {
namespace {

using StatusHelpers::StatusIs;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

class MockTlsCertificateSelectorContext : public Ssl::TlsCertificateSelectorContext {
public:
  ~MockTlsCertificateSelectorContext() override = default;
  MOCK_METHOD(const std::vector<Ssl::TlsContext>&, getTlsContexts, (), (const));
};

class FilterStateTest : public ::testing::Test {
protected:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr> create(const std::string& config_yaml,
                                                               bool for_quic = false) {
    envoy::extensions::transport_sockets::tls::cert_selectors::filter_state::v3::Config config;
    TestUtility::loadFromYaml(config_yaml, config);
    Ssl::TlsCertificateSelectorConfigFactory& provider_factory =
        Config::Utility::getAndCheckFactoryByName<Ssl::TlsCertificateSelectorConfigFactory>(
            "envoy.tls.certificate_selectors.filter_state");
    EXPECT_CALL(server_context_, disableStatelessSessionResumption())
        .WillRepeatedly(Return(disable_stateless_resumption_));
    EXPECT_CALL(server_context_, disableStatefulSessionResumption())
        .WillRepeatedly(Return(disable_stateful_resumption_));
    return provider_factory.createTlsCertificateSelectorFactory(config, factory_context_,
                                                                server_context_, for_quic);
  }

  // Creates a selector AND stores the factory as a member so the selector's references
  // (cert_contexts_, etc.) don't dangle.
  Ssl::TlsCertificateSelectorPtr createSelector() {
    auto factory = create(defaultConfig());
    EXPECT_TRUE(factory.ok());
    selector_factory_ = std::move(factory.value());
    return selector_factory_->create(selector_context_);
  }

  // Set up an SSL object with ex_data pointing to mock TransportSocketCallbacks.
  void setupSslWithFilterState(const StreamInfo::FilterStateSharedPtr& filter_state) {
    ssl_ctx_.reset(SSL_CTX_new(TLS_method()));
    ssl_.reset(SSL_new(ssl_ctx_.get()));

    // Mock chain: callbacks -> connection -> streamInfo -> filterState
    EXPECT_CALL(mock_callbacks_, connection()).WillRepeatedly(ReturnRef(mock_connection_));
    EXPECT_CALL(mock_connection_, streamInfo()).WillRepeatedly(ReturnRef(mock_stream_info_));
    EXPECT_CALL(mock_stream_info_, filterState()).WillRepeatedly(ReturnRef(filter_state));

    // Store the mock callbacks as ex_data on the SSL object (same as SslSocket does).
    SSL_set_ex_data(ssl_.get(), ContextImpl::sslSocketIndex(),
                    static_cast<Network::TransportSocketCallbacks*>(&mock_callbacks_));
  }

  // Build a minimal SSL_CLIENT_HELLO from our SSL object.
  // Only the `ssl` field is used by our selector.
  SSL_CLIENT_HELLO buildClientHello() {
    SSL_CLIENT_HELLO hello{};
    hello.ssl = ssl_.get();
    return hello;
  }

  std::string defaultConfig() const {
    return R"EOF(
      certificate_mapper:
        name: sni
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
          default_value: fallback
    )EOF";
  }

  std::string readTestFile(const std::string& name) {
    return TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath("test/config/integration/certs/" + name));
  }

  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context_;
  NiceMock<Ssl::MockServerContextConfig> server_context_;
  NiceMock<MockTlsCertificateSelectorContext> selector_context_;
  NiceMock<Network::MockTransportSocketCallbacks> mock_callbacks_;
  NiceMock<Network::MockConnection> mock_connection_;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info_;

  // Factory must outlive the selector (selector holds references to factory members).
  Ssl::TlsCertificateSelectorFactoryPtr selector_factory_;

  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  bssl::UniquePtr<SSL> ssl_;

  bool disable_stateless_resumption_{true};
  bool disable_stateful_resumption_{true};
};

// --- Config factory tests ---

TEST_F(FilterStateTest, BasicLoadTest) { EXPECT_OK(create(defaultConfig())); }

TEST_F(FilterStateTest, RejectsQuic) {
  EXPECT_THAT(create(defaultConfig(), true), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterStateTest, RejectsStatelessResumption) {
  disable_stateless_resumption_ = false;
  EXPECT_THAT(create(defaultConfig()), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterStateTest, RejectsStatefulResumption) {
  disable_stateful_resumption_ = false;
  EXPECT_THAT(create(defaultConfig()), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterStateTest, QuicPanic) {
  auto selector = createSelector();
  bool sni;
  Ssl::CurveNIDVector curve;
  EXPECT_DEATH(selector->findTlsContext("", curve, false, &sni), "Not supported with QUIC");
}

TEST_F(FilterStateTest, ProvidesCertificates) {
  auto selector = createSelector();
  EXPECT_TRUE(selector->providesCertificates());
}

TEST_F(FilterStateTest, CustomFilterStateKeys) {
  const std::string config_yaml = R"EOF(
    certificate_mapper:
      name: sni
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
        default_value: fallback
    cert_chain_filter_state_key: "my.custom.cert"
    private_key_filter_state_key: "my.custom.key"
    max_cache_size: 100
  )EOF";
  EXPECT_OK(create(config_yaml));
}

// --- selectTlsContext tests ---

TEST_F(FilterStateTest, NoCallbacksReturnsFailed) {
  auto selector = createSelector();

  // Create SSL object without setting ex_data — no TransportSocketCallbacks available.
  ssl_ctx_.reset(SSL_CTX_new(TLS_method()));
  ssl_.reset(SSL_new(ssl_ctx_.get()));
  auto hello = buildClientHello();
  auto result = selector->selectTlsContext(hello, nullptr);
  EXPECT_EQ(result.status, Ssl::SelectionResult::SelectionStatus::Failed);
}

TEST_F(FilterStateTest, NoFilterStateReturnsFailed) {
  auto selector = createSelector();

  // Set up SSL with filter state that has NO cert PEM.
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  setupSslWithFilterState(filter_state);

  auto hello = buildClientHello();
  auto result = selector->selectTlsContext(hello, nullptr);
  EXPECT_EQ(result.status, Ssl::SelectionResult::SelectionStatus::Failed);
}

TEST_F(FilterStateTest, MissingPrivateKeyReturnsFailed) {
  auto selector = createSelector();

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  // Set cert but not key.
  filter_state->setData("envoy.tls.certificate.cert_chain",
                        std::make_shared<Router::StringAccessorImpl>("some-cert-pem"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::Connection);
  setupSslWithFilterState(filter_state);

  auto hello = buildClientHello();
  auto result = selector->selectTlsContext(hello, nullptr);
  EXPECT_EQ(result.status, Ssl::SelectionResult::SelectionStatus::Failed);
}

TEST_F(FilterStateTest, InvalidPemReturnsFailed) {
  auto selector = createSelector();

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData("envoy.tls.certificate.cert_chain",
                        std::make_shared<Router::StringAccessorImpl>("not-valid-pem"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData("envoy.tls.certificate.private_key",
                        std::make_shared<Router::StringAccessorImpl>("not-valid-pem"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::Connection);
  setupSslWithFilterState(filter_state);

  auto hello = buildClientHello();
  auto result = selector->selectTlsContext(hello, nullptr);
  EXPECT_EQ(result.status, Ssl::SelectionResult::SelectionStatus::Failed);
}

TEST_F(FilterStateTest, ValidPemReturnsSuccess) {
  auto selector = createSelector();

  const std::string cert_pem = readTestFile("servercert.pem");
  const std::string key_pem = readTestFile("serverkey.pem");

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData(
      "envoy.tls.certificate.cert_chain", std::make_shared<Router::StringAccessorImpl>(cert_pem),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData(
      "envoy.tls.certificate.private_key", std::make_shared<Router::StringAccessorImpl>(key_pem),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);
  setupSslWithFilterState(filter_state);

  auto hello = buildClientHello();
  auto result = selector->selectTlsContext(hello, nullptr);
  EXPECT_EQ(result.status, Ssl::SelectionResult::SelectionStatus::Success);
  EXPECT_NE(result.selected_ctx, nullptr);
  EXPECT_NE(result.handle, nullptr);
}

TEST_F(FilterStateTest, CacheHitOnSecondCall) {
  auto selector = createSelector();

  const std::string cert_pem = readTestFile("servercert.pem");
  const std::string key_pem = readTestFile("serverkey.pem");

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData(
      "envoy.tls.certificate.cert_chain", std::make_shared<Router::StringAccessorImpl>(cert_pem),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);
  filter_state->setData(
      "envoy.tls.certificate.private_key", std::make_shared<Router::StringAccessorImpl>(key_pem),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);
  setupSslWithFilterState(filter_state);

  auto hello = buildClientHello();
  auto result1 = selector->selectTlsContext(hello, nullptr);
  EXPECT_EQ(result1.status, Ssl::SelectionResult::SelectionStatus::Success);
  const auto* ctx1 = result1.selected_ctx;

  auto result2 = selector->selectTlsContext(hello, nullptr);
  EXPECT_EQ(result2.status, Ssl::SelectionResult::SelectionStatus::Success);
  EXPECT_EQ(result2.selected_ctx, ctx1);
}

// --- onConfigUpdate tests ---

TEST_F(FilterStateTest, OnConfigUpdateReturnsOk) {
  auto factory_result = create(defaultConfig());
  ASSERT_TRUE(factory_result.ok());
  EXPECT_TRUE(factory_result.value()->onConfigUpdate().ok());
}

} // namespace
} // namespace FilterState
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
