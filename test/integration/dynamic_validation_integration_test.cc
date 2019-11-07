#include <fstream>

#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// This fake filter is used by CdsProtocolOptionsRejected.
class TestDynamicValidationNetworkFilter : public Network::WriteFilter {
public:
  Network::FilterStatus onWrite(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
};

class TestDynamicValidationNetworkFilterConfigFactory
    : public Extensions::NetworkFilters::Common::FactoryBase<
          envoy::config::filter::network::tcp_proxy::v2::TcpProxy> {
public:
  TestDynamicValidationNetworkFilterConfigFactory()
      : Extensions::NetworkFilters::Common::FactoryBase<
            envoy::config::filter::network::tcp_proxy::v2::TcpProxy>(
            "envoy.test.dynamic_validation") {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& /*proto_config*/,
      Server::Configuration::FactoryContext& /*context*/) override {
    return Network::FilterFactoryCb();
  }

  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsTyped(
      const envoy::config::filter::network::tcp_proxy::v2::TcpProxy&) override {
    return nullptr;
  }
};

REGISTER_FACTORY(TestDynamicValidationNetworkFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

// Pretty-printing of parameterized test names.
std::string dynamicValidationTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return fmt::format(
      "{}_{}",
      TestUtility::ipTestParamsToString(
          ::testing::TestParamInfo<Network::Address::IpVersion>(std::get<0>(params.param), 0)),
      std::get<1>(params.param) ? "with_reject_unknown_fields" : "without_reject_unknown_fields");
}

// Validate unknown field handling in dynamic configuration.
class DynamicValidationIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest {
public:
  DynamicValidationIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, std::get<0>(GetParam())),
        reject_unknown_dynamic_fields_(std::get<1>(GetParam())) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void createEnvoy() override {
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createApiTestServer(api_filesystem_config_, {"http"}, reject_unknown_dynamic_fields_,
                        reject_unknown_dynamic_fields_, allow_lds_rejection_);
  }

  ApiFilesystemConfig api_filesystem_config_;
  const bool reject_unknown_dynamic_fields_;
  bool allow_lds_rejection_{};
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, DynamicValidationIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    dynamicValidationTestParamsToString);

// Protocol options in CDS with unknown fields are rejected if and only if strict.
TEST_P(DynamicValidationIntegrationTest, CdsProtocolOptionsRejected) {
  api_filesystem_config_ = {
      "test/config/integration/server_xds.bootstrap.yaml",
      "test/config/integration/server_xds.cds.with_unknown_field.yaml",
      "test/config/integration/server_xds.eds.yaml",
      "test/config/integration/server_xds.lds.yaml",
      "test/config/integration/server_xds.rds.yaml",
  };
  initialize();
  if (reject_unknown_dynamic_fields_) {
    EXPECT_EQ(0, test_server_->counter("cluster_manager.cds.update_success")->value());
    // CDS API parsing will reject due to unknown HCM field.
    EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_rejected")->value());
    EXPECT_EQ(0, test_server_->counter("server.dynamic_unknown_fields")->value());
  } else {
    EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("server.dynamic_unknown_fields")->value());
  }
}

// Network filters in LDS with unknown fields are rejected if and only if strict.
TEST_P(DynamicValidationIntegrationTest, LdsFilterRejected) {
  allow_lds_rejection_ = true;
  api_filesystem_config_ = {
      "test/config/integration/server_xds.bootstrap.yaml",
      "test/config/integration/server_xds.cds.yaml",
      "test/config/integration/server_xds.eds.yaml",
      "test/config/integration/server_xds.lds.with_unknown_field.yaml",
      "test/config/integration/server_xds.rds.yaml",
  };
  initialize();
  if (reject_unknown_dynamic_fields_) {
    EXPECT_EQ(0, test_server_->counter("listener_manager.lds.update_success")->value());
    // LDS API parsing will reject due to unknown HCM field.
    EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_rejected")->value());
    EXPECT_EQ(nullptr, test_server_->counter("http.router.rds.route_config_0.update_success"));
    EXPECT_EQ(0, test_server_->counter("server.dynamic_unknown_fields")->value());
  } else {
    EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("http.router.rds.route_config_0.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("server.dynamic_unknown_fields")->value());
  }
  EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.update_success")->value());
}

// Network filters in LDS config using TypedStruct with unknown fields are rejected if and only if
// strict.
TEST_P(DynamicValidationIntegrationTest, LdsFilterRejectedTypedStruct) {
  allow_lds_rejection_ = true;
  api_filesystem_config_ = {
      "test/config/integration/server_xds.bootstrap.yaml",
      "test/config/integration/server_xds.cds.yaml",
      "test/config/integration/server_xds.eds.yaml",
      "test/config/integration/server_xds.lds.with_unknown_field.typed_struct.yaml",
      "test/config/integration/server_xds.rds.yaml",
  };
  initialize();
  if (reject_unknown_dynamic_fields_) {
    EXPECT_EQ(0, test_server_->counter("listener_manager.lds.update_success")->value());
    // LDS API parsing will reject due to unknown HCM field.
    EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_rejected")->value());
    EXPECT_EQ(nullptr, test_server_->counter("http.router.rds.route_config_0.update_success"));
    EXPECT_EQ(0, test_server_->counter("server.dynamic_unknown_fields")->value());
  } else {
    EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("http.router.rds.route_config_0.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("server.dynamic_unknown_fields")->value());
  }
  EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.update_success")->value());
}

// Unknown fields in RDS cause config load failure if and only if strict.
TEST_P(DynamicValidationIntegrationTest, RdsFailedBySubscription) {
  api_filesystem_config_ = {
      "test/config/integration/server_xds.bootstrap.yaml",
      "test/config/integration/server_xds.cds.yaml",
      "test/config/integration/server_xds.eds.yaml",
      "test/config/integration/server_xds.lds.yaml",
      "test/config/integration/server_xds.rds.with_unknown_field.yaml",
  };
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());
  if (reject_unknown_dynamic_fields_) {
    EXPECT_EQ(0, test_server_->counter("http.router.rds.route_config_0.update_success")->value());
    // Unknown fields in the config result in the update_rejected counter incremented
    EXPECT_EQ(1, test_server_->counter("http.router.rds.route_config_0.update_rejected")->value());
    EXPECT_EQ(0, test_server_->counter("server.dynamic_unknown_fields")->value());
  } else {
    EXPECT_EQ(1, test_server_->counter("http.router.rds.route_config_0.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("server.dynamic_unknown_fields")->value());
  }
  EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.update_success")->value());
}

// Unknown fields in EDS cause config load failure if and only if strict.
TEST_P(DynamicValidationIntegrationTest, EdsFailedBySubscription) {
  api_filesystem_config_ = {
      "test/config/integration/server_xds.bootstrap.yaml",
      "test/config/integration/server_xds.cds.yaml",
      "test/config/integration/server_xds.eds.with_unknown_field.yaml",
      "test/config/integration/server_xds.lds.yaml",
      "test/config/integration/server_xds.rds.yaml",
  };
  initialize();
  EXPECT_EQ(1, test_server_->counter("listener_manager.lds.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("http.router.rds.route_config_0.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("cluster_manager.cds.update_success")->value());
  if (reject_unknown_dynamic_fields_) {
    EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.update_success")->value());
    // Unknown fields in the config result in the update_rejected counter incremented
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.update_rejected")->value());
    EXPECT_EQ(0, test_server_->counter("server.dynamic_unknown_fields")->value());
  } else {
    EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.update_success")->value());
    EXPECT_EQ(1, test_server_->counter("server.dynamic_unknown_fields")->value());
  }
}

} // namespace
} // namespace Envoy
