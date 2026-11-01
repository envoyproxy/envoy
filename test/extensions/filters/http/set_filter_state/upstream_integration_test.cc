#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/retry/host/previous_hosts/v3/previous_hosts.pb.h"

#include "source/common/config/metadata.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetFilterState {
namespace {

class SetFilterStateUpstreamIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  SetFilterStateUpstreamIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    setUpstreamCount(2);
    setUpstreamProtocol(Http::CodecType::HTTP1);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* locality = bootstrap.mutable_static_resources()
                           ->mutable_clusters(0)
                           ->mutable_load_assignment()
                           ->mutable_endpoints(0);
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
      for (int i = 0; i < locality->lb_endpoints_size(); ++i) {
        Config::Metadata::mutableMetadataValue(
            *locality->mutable_lb_endpoints(i)->mutable_metadata(), "test.set_filter_state", "host")
            .set_string_value(absl::StrCat("host-", i));
      }
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* retry_policy = hcm.mutable_route_config()
                                   ->mutable_virtual_hosts(0)
                                   ->mutable_routes(0)
                                   ->mutable_route()
                                   ->mutable_retry_policy();
          retry_policy->set_retry_on("5xx");
          retry_policy->mutable_num_retries()->set_value(1);
          retry_policy->set_host_selection_retry_max_attempts(5);
          auto* host_predicate = retry_policy->add_retry_host_predicate();
          host_predicate->set_name("envoy.retry_host_predicates.previous_hosts");
          std::ignore = host_predicate->mutable_typed_config()->PackFrom(
              envoy::extensions::retry::host::previous_hosts::v3::PreviousHostsPredicate());
        });

    // prependFilter inserts at the front, so set_filter_state runs before header_mutation.
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.header_mutation
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
  mutations:
    request_mutations:
    - append:
        header:
          key: x-upstream-host
          value: "%FILTER_STATE(test.upstream_host:PLAIN)%"
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
)EOF",
                                 false);
    // clear_route_cache has no downstream callbacks to act on upstream and must be a no-op.
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
  on_request_headers:
  - object_key: test.upstream_host
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "%UPSTREAM_METADATA(test.set_filter_state:host)%"
  clear_route_cache: true
)EOF",
                                 false);
    useAccessLog("%FILTER_STATE(test.upstream_host:PLAIN)%");
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SetFilterStateUpstreamIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SetFilterStateUpstreamIntegrationTest, EachAttemptOverwritesValue) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  const std::optional<uint64_t> first = waitForNextUpstreamRequest({0, 1});
  ASSERT_TRUE(first.has_value());
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader("x-upstream-host", absl::StrCat("host-", *first)));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  FakeHttpConnectionPtr first_connection = std::move(fake_upstream_connection_);
  const uint64_t second = 1 - *first;
  waitForNextUpstreamRequest(second);
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader("x-upstream-host", absl::StrCat("host-", second)));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(absl::StrCat("host-", second), waitForAccessLog(access_log_name_));

  ASSERT_TRUE(first_connection->close());
  ASSERT_TRUE(first_connection->waitForDisconnect());
}

} // namespace
} // namespace SetFilterState
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
