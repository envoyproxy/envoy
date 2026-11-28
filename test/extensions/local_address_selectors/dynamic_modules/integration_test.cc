#include <filesystem>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/local_address_selectors/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace {

// The IP version, and the language of the test module: the C module exercises the ABI directly and
// the Rust one goes through the SDK. Both implement the same selector names.
using IntegrationTestParams = std::tuple<Network::Address::IpVersion, std::string>;

std::string paramsToString(const testing::TestParamInfo<IntegrationTestParams>& params) {
  return absl::StrCat(TestUtility::ipVersionToString(std::get<0>(params.param)), "_",
                      std::get<1>(params.param));
}

class DynamicModuleLocalAddressSelectorIntegrationTest
    : public testing::TestWithParam<IntegrationTestParams>,
      public HttpIntegrationTest {
public:
  DynamicModuleLocalAddressSelectorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {}

  // Sets the dynamic module selector on the upstream cluster's bind config and starts Envoy.
  void initializeWithSelector(const std::string& selector_name,
                              const std::string& selector_config = "") {
    const std::string& language = std::get<1>(GetParam());
    const std::string module_name =
        language == "rust" ? "local_address_selector_integration_test" : "local_address_selector";
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath(module_name, language);
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               std::filesystem::path(shared_object_path).parent_path().string(), 1);

    config_helper_.addConfigModifier([module_name, selector_name, selector_config](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::local_address_selectors::dynamic_modules::v3::
          DynamicModuleLocalAddressSelector proto;
      proto.mutable_dynamic_module_config()->set_name(module_name);
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

  // Sets a source address in the upstream cluster's bind config.
  void setBindConfigSourceAddress(const std::string& address) {
    config_helper_.addConfigModifier([address](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* source_address = bootstrap.mutable_static_resources()
                                 ->mutable_clusters(0)
                                 ->mutable_upstream_bind_config()
                                 ->mutable_source_address();
      source_address->set_address(address);
      source_address->set_port_value(0);
    });
  }

  std::string upstreamPeerAddress() {
    return fake_upstream_connection_->connection()
        .connectionInfoProvider()
        .remoteAddress()
        ->ip()
        ->addressAsString();
  }

  uint64_t counter(const std::string& name) {
    return test_server_->counter("cluster.cluster_0." + name)->value();
  }

  // Linux routes all of 127.0.0.0/8 to loopback. Other platforms only configure 127.0.0.1, and
  // IPv6 has only ::1, which is also the default source address.
  bool hasSpareLoopbackAddresses() {
#ifdef __linux__
    return version_ == Network::Address::IpVersion::v4;
#else
    return false;
#endif
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndLanguages, DynamicModuleLocalAddressSelectorIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values("c", "rust")),
    paramsToString);

// The upstream sees the source address the module picked.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, UsesSelectedSourceAddress) {
  if (!hasSpareLoopbackAddresses()) {
    GTEST_SKIP() << "needs a second loopback address";
  }
  initializeWithSelector("select", "127.0.0.2");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  EXPECT_EQ("127.0.0.2", upstreamPeerAddress());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, counter("dynamic_module_local_address_selector.selected"));
  EXPECT_EQ(0, counter("dynamic_module_local_address_selector.default"));
}

// Keeping the default selection uses the source address of the bind config.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, KeepsBindConfigSourceAddress) {
  if (!hasSpareLoopbackAddresses()) {
    GTEST_SKIP() << "needs a second loopback address";
  }
  setBindConfigSourceAddress("127.0.0.3");
  initializeWithSelector("default");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  EXPECT_EQ("127.0.0.3", upstreamPeerAddress());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, counter("dynamic_module_local_address_selector.default"));
  EXPECT_EQ(0, counter("dynamic_module_local_address_selector.selected"));
}

// Keeping the default selection without a bind config source address proxies normally.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, UseDefault) {
  initializeWithSelector("default");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, counter("dynamic_module_local_address_selector.default"));
}

// A failed selection closes the upstream connection before it connects.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, FailsConnection) {
  initializeWithSelector("fail");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_connect_fail", 1);
  EXPECT_EQ(counter("upstream_cx_total"), counter("dynamic_module_local_address_selector.failed"));
  EXPECT_EQ(0, counter("dynamic_module_local_address_selector.selected"));
}

// With preconnect, a selector that fails every connection makes the pool open at most the
// connections it preconnects for the request. The failure fails the request, so the pool has no
// stream left to preconnect for and stops.
TEST_P(DynamicModuleLocalAddressSelectorIntegrationTest, FailWithPreconnectDoesNotChurn) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->mutable_preconnect_policy()
        ->mutable_per_upstream_preconnect_ratio()
        ->set_value(3.0);
  });
  initializeWithSelector("fail");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (int request = 1; request <= 2; ++request) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_connect_fail", request);

    // Give a churning pool time to show itself, then check it stopped.
    const uint64_t connections = counter("upstream_cx_total");
    timeSystem().advanceTimeWait(std::chrono::milliseconds(100));
    EXPECT_EQ(connections, counter("upstream_cx_total"));
    EXPECT_LE(connections, static_cast<uint64_t>(3 * request));
  }
  EXPECT_EQ(counter("upstream_cx_total"), counter("dynamic_module_local_address_selector.failed"));
}

} // namespace
} // namespace Envoy
