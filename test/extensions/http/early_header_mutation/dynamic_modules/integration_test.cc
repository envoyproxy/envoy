#include <filesystem>
#include <string>
#include <vector>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/http/early_header_mutation/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/common/fmt.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

using HttpConnectionManager =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

// upstream_request_->headers() is a real RequestHeaderMap rather than a test header map, so the
// test-only accessors are unavailable. Returns the first value for the key, or "" when absent.
std::string headerValue(const Http::RequestHeaderMap& headers, absl::string_view key) {
  const auto values = headers.get(Http::LowerCaseString(key));
  return values.empty() ? "" : std::string(values[0]->value().getStringView());
}

// Drives the extension end to end: proto -> factory -> C++ extension -> ABI -> module. The module
// under test is a parameter so the same assertions cover every SDK language.
class DynamicModuleEarlyHeaderMutationIntegrationTest : public testing::TestWithParam<std::string>,
                                                        public HttpIntegrationTest {
public:
  DynamicModuleEarlyHeaderMutationIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  static std::string paramName(const testing::TestParamInfo<std::string>& info) {
    return info.param;
  }

  // Adds one early_header_mutation_extensions entry loading `module_name` with `mutation_name`.
  void addMutation(absl::string_view module_name, absl::string_view mutation_name,
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
  early_header_mutation_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: {}
)EOF",
                                 module_config);
    }
    const std::string yaml = fmt::format(R"EOF(
name: envoy.http.early_header_mutation.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.http.early_header_mutation.dynamic_modules.v3.DynamicModuleEarlyHeaderMutation
  dynamic_module_config:
    name: {}
    do_not_close: true
  early_header_mutation_name: {}{}
)EOF",
                                         module_name, mutation_name, config_field);

    envoy::config::core::v3::TypedExtensionConfig extension;
    TestUtility::loadFromYaml(yaml, extension);
    config_helper_.addConfigModifier([extension](HttpConnectionManager& hcm) {
      hcm.add_early_header_mutation_extensions()->CopyFrom(extension);
    });
  }

  // The SDK language for this test parameter. Each language ships a module of the same name.
  const std::string& language() { return GetParam(); }
  static constexpr absl::string_view integrationModule() {
    return "early_header_mutation_integration_test";
  }
};

// The module rewrites the request headers, and because early header mutation runs before routing
// the rewritten headers reach the upstream.
TEST_P(DynamicModuleEarlyHeaderMutationIntegrationTest, MutatesRequestHeadersSeenByUpstream) {
  addMutation(integrationModule(), "test_mutation", "marker-value");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},        {":path", "/"},
                                                 {":scheme", "http"},       {":authority", "host"},
                                                 {"x-test-input", "hello"}, {"x-remove-me", "bye"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  const auto& upstream = upstream_request_->headers();
  EXPECT_EQ("hello", headerValue(upstream, "x-dynamic-module-echo"));
  EXPECT_EQ("marker-value", headerValue(upstream, "x-dynamic-module-marker"));
  EXPECT_EQ("yes", headerValue(upstream, "x-dynamic-module-removed"));
  EXPECT_TRUE(upstream.get(Http::LowerCaseString("x-remove-me")).empty());
  EXPECT_EQ(2, upstream.get(Http::LowerCaseString("x-dynamic-module-added")).size());
  EXPECT_EQ("two:2", headerValue(upstream, "x-dynamic-module-multi"));
  // Read-only stream info is reachable this early.
  EXPECT_EQ("HTTP/1.1", headerValue(upstream, "x-dynamic-module-protocol"));
  EXPECT_EQ("yes", headerValue(upstream, "x-dynamic-module-connection-id-present"));
  // But the response attributes are not populated yet.
  EXPECT_EQ("yes", headerValue(upstream, "x-dynamic-module-response-code-absent"));

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  codec_client_->close();
}

// The single shared configuration serves many requests across worker threads.
TEST_P(DynamicModuleEarlyHeaderMutationIntegrationTest, SharedConfigServesManyRequests) {
  addMutation(integrationModule(), "counting");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  for (int i = 0; i < 3; i++) {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    // The counter is module-global, so it is non-empty on every request.
    EXPECT_FALSE(headerValue(upstream_request_->headers(), "x-dynamic-module-calls").empty());
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  codec_client_->close();
}

INSTANTIATE_TEST_SUITE_P(Languages, DynamicModuleEarlyHeaderMutationIntegrationTest,
                         testing::Values("rust", "cpp", "go"),
                         DynamicModuleEarlyHeaderMutationIntegrationTest::paramName);

// Chain semantics are language independent, so they are covered once with the C fixtures.
class DynamicModuleEarlyHeaderMutationChainTest : public testing::Test, public HttpIntegrationTest {
public:
  DynamicModuleEarlyHeaderMutationChainTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void addMutation(absl::string_view module_name) {
    const std::string search_path =
        std::filesystem::path(
            Extensions::DynamicModules::testSharedObjectPath(std::string(module_name), "c"))
            .parent_path()
            .string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", search_path, 1);
    const std::string yaml = fmt::format(R"EOF(
name: envoy.http.early_header_mutation.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.http.early_header_mutation.dynamic_modules.v3.DynamicModuleEarlyHeaderMutation
  dynamic_module_config:
    name: {}
    do_not_close: true
  early_header_mutation_name: chain
)EOF",
                                         module_name);
    envoy::config::core::v3::TypedExtensionConfig extension;
    TestUtility::loadFromYaml(yaml, extension);
    config_helper_.addConfigModifier([extension](HttpConnectionManager& hcm) {
      hcm.add_early_header_mutation_extensions()->CopyFrom(extension);
    });
  }

  Http::TestRequestHeaderMapImpl defaultRequest() {
    return {{":method", "GET"},
            {":path", "/"},
            {":scheme", "http"},
            {":authority", "host"},
            {"x-test-input", "hello"}};
  }
};

// Returning false from the first extension suppresses the ones configured after it, while keeping
// the mutations it already made.
TEST_F(DynamicModuleEarlyHeaderMutationChainTest, ReturningFalseStopsTheChain) {
  addMutation("early_header_mutation_stop_chain");
  addMutation("early_header_mutation_rewrite");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto request_headers = defaultRequest();
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  const auto& upstream = upstream_request_->headers();
  EXPECT_EQ("ran", headerValue(upstream, "x-dynamic-module-first"));
  // The second extension never ran, so it left no trace.
  EXPECT_TRUE(upstream.get(Http::LowerCaseString("x-dynamic-module-echo")).empty());

  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
}

// Returning true lets the next extension run.
TEST_F(DynamicModuleEarlyHeaderMutationChainTest, ReturningTrueContinuesTheChain) {
  addMutation("early_header_mutation_rewrite");
  addMutation("early_header_mutation_stop_chain");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto request_headers = defaultRequest();
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  const auto& upstream = upstream_request_->headers();
  EXPECT_EQ("hello", headerValue(upstream, "x-dynamic-module-echo"));
  EXPECT_EQ("ran", headerValue(upstream, "x-dynamic-module-first"));

  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
}

} // namespace
} // namespace Envoy
