#include "envoy/extensions/transport_sockets/tls/handshakers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "source/common/tls/context_manager_impl.h"

#include "test/common/tls/integration/ssl_integration_test_base.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {
namespace {

constexpr char kCModulesPath[] = "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c";

// Verifies that a downstream TLS listener can use a dynamic module handshaker to complete the
// handshake and serve requests.
class DynamicModuleHandshakerIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public SslIntegrationTestBase {
public:
  DynamicModuleHandshakerIntegrationTest() : SslIntegrationTestBase(GetParam()) {}
  void TearDown() override { SslIntegrationTestBase::TearDown(); }

  void setModuleName(const std::string& name) { module_name_ = name; }

  void initialize() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(kCModulesPath), 1);
    config_helper_.addSslConfig();
    const std::string module_name = module_name_;
    config_helper_.addConfigModifier(
        [module_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* transport_socket = bootstrap.mutable_static_resources()
                                       ->mutable_listeners(0)
                                       ->mutable_filter_chains(0)
                                       ->mutable_transport_socket();
          envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
          RELEASE_ASSERT(transport_socket->mutable_typed_config()->UnpackTo(&tls_context),
                         "failed to unpack downstream TLS context");
          auto* custom_handshaker =
              tls_context.mutable_common_tls_context()->mutable_custom_handshaker();
          custom_handshaker->set_name("envoy.tls.handshakers.dynamic_modules");
          envoy::extensions::transport_sockets::tls::handshakers::dynamic_modules::v3::
              DynamicModuleTlsHandshaker handshaker;
          handshaker.mutable_dynamic_module_config()->set_name(module_name);
          handshaker.set_handshaker_name("test");
          std::ignore = custom_handshaker->mutable_typed_config()->PackFrom(handshaker);
          std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
        });
    HttpIntegrationTest::initialize();
    context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        server_factory_context_);
    registerTestServerPorts({"http"});
  }

private:
  std::string module_name_{"tls_handshaker_custom"};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleHandshakerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The module drives the handshake from its handshake hook.
TEST_P(DynamicModuleHandshakerIntegrationTest, ModuleDrivenHandshake) {
  initialize();
  codec_client_ = makeHttpConnection(makeSslClientConnection({}));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  checkStats();
}

// The module provides a config only, so Envoy drives the standard handshake.
TEST_P(DynamicModuleHandshakerIntegrationTest, DefaultHandshake) {
  setModuleName("tls_handshaker_no_op");
  initialize();
  codec_client_ = makeHttpConnection(makeSslClientConnection({}));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  checkStats();
}

} // namespace
} // namespace Ssl
} // namespace Envoy
