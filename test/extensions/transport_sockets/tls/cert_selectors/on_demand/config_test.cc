#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"

#include "source/common/config/utility.h"
#include "source/common/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

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

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
