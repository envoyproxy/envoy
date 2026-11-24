#include <filesystem>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/local_address_selectors/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace {

class DynamicModuleLocalAddressSelectorIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleLocalAddressSelectorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  // Sets the dynamic module selector on the upstream cluster's bind config.
  void initializeWithSelector(const std::string& selector_name,
                              const std::string& selector_config = "") {
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("local_address_selector", "c");
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               std::filesystem::path(shared_object_path).parent_path().string(), 1);

    config_helper_.addConfigModifier(
        [selector_name, selector_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          envoy::extensions::local_address_selectors::dynamic_modules::v3::
              DynamicModuleLocalAddressSelector proto;
          proto.mutable_dynamic_module_config()->set_name("local_address_selector");
          proto.mutable_dynamic_module_config()->set_do_not_close(true);
          proto.set_selector_name(selector_name);
          if (!selector_config.empty()) {
            Protobuf::StringValue value;
            value.set_value(selector_config);
            std::ignore = proto.mutable_selector_config()->PackFrom(value);
          }
          auto* selector = bootstrap.mutable_static_resources()
                               ->mutable_clusters(0)
                               ->mutable_upstream_bind_config()
                               ->mutable_local_address_selector();
          selector->set_name("envoy.upstream.local_address_selector.dynamic_modules");
          std::ignore = selector->mutable_typed_config()->PackFrom(proto);
        });
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleLocalAddressSelectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The upstream sees the source address the module picked.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, UsesSelectedSourceAddress) {
  // Linux routes all of 127.0.0.0/8 to loopback. Other platforms only configure 127.0.0.1, and IPv6
  // has only ::1, which is also the default.
#ifndef __linux__
  GTEST_SKIP() << "needs a second loopback address";
#endif
  if (version_ != Network::Address::IpVersion::v4) {
    GTEST_SKIP() << "needs a second loopback address";
  }
  initializeWithSelector("select", "127.0.0.2");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  EXPECT_EQ("127.0.0.2", fake_upstream_connection_->connection()
                             .connectionInfoProvider()
                             .remoteAddress()
                             ->ip()
                             ->addressAsString());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// UseDefault keeps the bind config behavior, which is no source address here.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, UseDefault) {
  initializeWithSelector("default");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// A failed selection closes the upstream connection before it connects.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, FailsConnection) {
  initializeWithSelector("fail");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_connect_fail", 1);
}

} // namespace
} // namespace Envoy
