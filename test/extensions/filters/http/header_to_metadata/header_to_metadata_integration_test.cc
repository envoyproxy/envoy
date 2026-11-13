#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using ProtoConfig = envoy::extensions::filters::http::header_to_metadata::v3::Config;
using testing::_;
using testing::Not;

class HeaderToMetadataIntegrationTest : public HttpProtocolIntegrationTest {
public:
  // Moves x-internal-<i> request headers and x-internal-response-<i> response headers to dynamic
  // metadata for each i below rule_count, and copies the host to dynamic metadata.
  void initializeFilter(uint32_t rule_count) {
    ProtoConfig config;
    for (uint32_t i = 0; i < rule_count; i++) {
      addRule(*config.add_request_rules(), absl::StrCat("x-internal-", i), "internal.request",
              true);
      addRule(*config.add_response_rules(), absl::StrCat("x-internal-response-", i),
              "internal.response", true);
    }
    addRule(*config.add_request_rules(), "host", "internal.request", false);
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter;
    filter.set_name("envoy.filters.http.header_to_metadata");
    std::ignore = filter.mutable_typed_config()->PackFrom(config);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(filter));
    useAccessLog("%DYNAMIC_METADATA(internal.request:x-internal-1)% "
                 "%DYNAMIC_METADATA(internal.response:x-internal-response-1)% "
                 "%DYNAMIC_METADATA(internal.request:host)%");
    initialize();
  }

  void addRule(ProtoConfig::Rule& rule, const std::string& header,
               const std::string& metadata_namespace, bool remove) {
    rule.set_header(header);
    rule.set_remove(remove);
    rule.mutable_on_header_present()->set_metadata_namespace(metadata_namespace);
    rule.mutable_on_header_present()->set_key(header);
  }

  // Sends a request and a response that each carry one internal header and one other header.
  void testInternalHeadersMoved() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
    request_headers.addCopy("x-internal-1", "request-value");
    request_headers.addCopy("x-other", "request-other");
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"x-internal-response-1", "response-value"},
                                                     {"x-other", "response-other"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

    EXPECT_THAT(upstream_request_->headers(), Not(ContainsHeader("x-internal-1", _)));
    EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-other", "request-other"));
    EXPECT_THAT(response->headers(), Not(ContainsHeader("x-internal-response-1", _)));
    EXPECT_THAT(response->headers(), ContainsHeader("x-other", "response-other"));
    EXPECT_EQ(absl::StrCat("request-value response-value ", request_headers.getHostValue()),
              waitForAccessLog(access_log_name_));
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, HeaderToMetadataIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// With many rules, the filter walks the headers to find the rules to apply.
TEST_P(HeaderToMetadataIntegrationTest, ManyRules) {
  initializeFilter(64);
  testInternalHeadersMoved();
}

// With few rules, the filter applies every rule.
TEST_P(HeaderToMetadataIntegrationTest, FewRules) {
  initializeFilter(2);
  testInternalHeadersMoved();
}

} // namespace
} // namespace Envoy
