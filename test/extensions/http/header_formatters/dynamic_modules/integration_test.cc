#include <filesystem>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/http/header_formatters/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/common/fmt.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

using HttpConnectionManager =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

// Drives the extension end to end: proto -> factory -> C++ extension -> ABI -> module. Header
// casing is only observable on the wire, so the exchanges below use raw connections on both sides
// rather than the codec-level helpers. The module under test is a parameter so the same assertions
// cover every SDK language.
class DynamicModuleHeaderFormatterIntegrationTest : public testing::TestWithParam<std::string>,
                                                    public HttpIntegrationTest {
public:
  DynamicModuleHeaderFormatterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  static std::string paramName(const testing::TestParamInfo<std::string>& info) {
    return info.param;
  }

  // Points ENVOY_DYNAMIC_MODULES_SEARCH_PATH at the directory holding the module for this
  // language, and installs the formatter on both the downstream connection manager and the
  // upstream cluster.
  //
  // The two sides are independent: the request header map carries the formatter the downstream
  // codec created, so the connection manager's copy decides the casing Envoy writes upstream,
  // while the cluster's copy decides the casing written back downstream.
  void setUpFormatter(absl::string_view module_name, absl::string_view formatter_name,
                      absl::string_view module_config = "") {
    const std::string search_path =
        std::filesystem::path(
            Extensions::DynamicModules::testSharedObjectPath(std::string(module_name), language()))
            .parent_path()
            .string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", search_path, 1);

    std::string config_field;
    if (!module_config.empty()) {
      config_field = fmt::format(R"EOF(
  header_formatter_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: {}
)EOF",
                                 module_config);
    }
    const std::string yaml = fmt::format(R"EOF(
name: envoy.http.stateful_header_formatters.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.http.header_formatters.dynamic_modules.v3.DynamicModuleHeaderFormatter
  dynamic_module_config:
    name: {}
    do_not_close: true
  header_formatter_name: {}{}
)EOF",
                                         module_name, formatter_name, config_field);

    envoy::config::core::v3::TypedExtensionConfig extension;
    TestUtility::loadFromYaml(yaml, extension);

    config_helper_.addConfigModifier([extension](HttpConnectionManager& hcm) {
      hcm.mutable_http_protocol_options()
          ->mutable_header_key_format()
          ->mutable_stateful_formatter()
          ->CopyFrom(extension);
    });
    config_helper_.addConfigModifier(
        [extension](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http_protocol_options()
              ->mutable_header_key_format()
              ->mutable_stateful_formatter()
              ->CopyFrom(extension);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  }

  // Sends `request` verbatim downstream and returns the bytes Envoy wrote to the upstream.
  std::string exchange(absl::string_view request, absl::string_view response,
                       IntegrationTcpClientPtr& tcp_client, std::string* downstream_response) {
    EXPECT_TRUE(tcp_client->write(std::string(request), false));

    FakeRawConnectionPtr upstream_connection;
    EXPECT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

    std::string upstream_request;
    EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("\r\n\r\n"),
                                                 &upstream_request));
    EXPECT_TRUE(upstream_connection->write(std::string(response)));

    if (downstream_response != nullptr) {
      tcp_client->waitForData("\r\n\r\n", false);
      *downstream_response = tcp_client->data();
    }
    return upstream_request;
  }

  // The SDK language for this test parameter. Each language ships a module of the same name.
  const std::string& language() { return GetParam(); }
  static constexpr absl::string_view integrationModule() {
    return "header_formatter_integration_test";
  }
};

// The module remembers the casing the peer used and restores it on the way out, in both
// directions, while headers Envoy adds itself take the module's other branch.
TEST_P(DynamicModuleHeaderFormatterIntegrationTest, RestoresObservedCasingInBothDirections) {
  setUpFormatter(integrationModule(), "preserve_case");
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  std::string downstream_response;
  const std::string upstream_request =
      exchange("GET / HTTP/1.1\r\nHost: host\r\nMy-Request-Header: foo\r\n\r\n",
               "HTTP/1.1 200 OK\r\ncontent-length: 0\r\nMy-Response-Header: bar\r\n\r\n",
               tcp_client, &downstream_response);

  // The downstream peer sent this key, so process_key saw it and format restored the spelling.
  EXPECT_TRUE(absl::StrContains(upstream_request, "My-Request-Header: foo")) << upstream_request;
  // Envoy adds this one itself, so the module never saw it and took its upper-casing branch.
  EXPECT_TRUE(absl::StrContains(upstream_request, "X-FORWARDED-PROTO:")) << upstream_request;

  // The response direction uses the formatter the upstream codec created from the cluster's
  // protocol options, which is a separate configuration and a separate instance.
  EXPECT_TRUE(absl::StrContains(downstream_response, "My-Response-Header: bar"))
      << downstream_response;

  tcp_client->close();
}

// The module configuration bytes reach the module: the configured key is upper-cased even though
// the peer sent it in mixed case.
TEST_P(DynamicModuleHeaderFormatterIntegrationTest, FormatterConfigReachesModule) {
  setUpFormatter(integrationModule(), "preserve_case", "my-request-header");
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  const std::string upstream_request =
      exchange("GET / HTTP/1.1\r\nHost: host\r\nMy-Request-Header: foo\r\n\r\n",
               "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", tcp_client, nullptr);

  EXPECT_TRUE(absl::StrContains(upstream_request, "MY-REQUEST-HEADER: foo")) << upstream_request;
  tcp_client->close();
}

// A module that declines to create a formatter must not fail the message: Envoy falls back to its
// default lower-cased header casing.
TEST_P(DynamicModuleHeaderFormatterIntegrationTest, DeclinedFormatterFallsBackToDefault) {
  setUpFormatter(integrationModule(), "decline_formatter");
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  std::string downstream_response;
  const std::string upstream_request =
      exchange("GET / HTTP/1.1\r\nHost: host\r\nMy-Request-Header: foo\r\n\r\n",
               "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", tcp_client, &downstream_response);

  EXPECT_TRUE(absl::StrContains(upstream_request, "my-request-header: foo")) << upstream_request;
  EXPECT_TRUE(absl::StrContains(downstream_response, "200")) << downstream_response;
  tcp_client->close();
}

#ifndef __SANITIZE_ADDRESS__
// TODO(wbpcode): address sanitizer cannot handle the cross shared libraries vptr casts.
// and we need to figure out a way to fix it.
auto DynamicModulesIntegrationTestValues = testing::Values("rust", "go", "cpp");
#else
auto DynamicModulesIntegrationTestValues = testing::Values("rust", "go");
#endif

INSTANTIATE_TEST_SUITE_P(Languages, DynamicModuleHeaderFormatterIntegrationTest,
                         DynamicModulesIntegrationTestValues,
                         DynamicModuleHeaderFormatterIntegrationTest::paramName);

} // namespace
} // namespace Envoy
