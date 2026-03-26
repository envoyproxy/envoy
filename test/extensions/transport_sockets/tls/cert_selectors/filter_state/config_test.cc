#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3/config.pb.h"

#include "source/common/config/utility.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/filter_state/config.h"

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
namespace FilterState {
namespace {

using StatusHelpers::StatusIs;
using ::testing::NiceMock;
using ::testing::Return;

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

  std::string defaultConfig() const {
    return R"EOF(
      certificate_mapper:
        name: sni
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
          default_value: fallback
    )EOF";
  }

  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context_;
  NiceMock<Ssl::MockServerContextConfig> server_context_;
  NiceMock<MockTlsCertificateSelectorContext> selector_context_;

  bool disable_stateless_resumption_{true};
  bool disable_stateful_resumption_{true};
};

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
  auto factory = create(defaultConfig());
  EXPECT_OK(factory);
  auto selector = factory.value()->create(selector_context_);
  bool sni;
  absl::InlinedVector<int, 3> curve;
  EXPECT_DEATH(selector->findTlsContext("", curve, false, &sni), "Not supported with QUIC");
}

TEST_F(FilterStateTest, ProvidesCertificates) {
  auto factory = create(defaultConfig());
  EXPECT_OK(factory);
  auto selector = factory.value()->create(selector_context_);
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

} // namespace
} // namespace FilterState
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
