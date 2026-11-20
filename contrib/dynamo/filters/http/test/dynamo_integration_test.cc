#include "source/common/buffer/buffer_impl.h"
#include "source/common/config/well_known_names.h"

#include "test/integration/http_integration.h"
#include "test/integration/tagged_stats_test_util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using testing::Eq;

class DynamoIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public HttpIntegrationTest {
public:
  DynamoIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  // Whether the stats store derives tags with the explicit-tags logic (the tag-friendly scope API)
  // rather than with the legacy tag-extraction rules. Both modes must produce identical stat names,
  // tag-extracted names and tags for the filter stats asserted below.
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v4; }

  void initialize() override {
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.dynamo
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamo.v3.Dynamo
)EOF");
    // Exercise both stats modes without doubling the test matrix: the two IP versions run the
    // server in different modes.
    Stats::TestUtil::setExplicitStatsTags(explicitTags());
    HttpIntegrationTest::initialize();
    Stats::TestUtil::verifyExplicitStatsTags(*test_server_, explicitTags());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamoIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamoIntegrationTest, OperationAndTableStatsAreTagged) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-amz-target", "DynamoDB_20120810.GetItem"}};
  auto response =
      codec_client_->makeRequestWithBody(request_headers, R"EOF({"TableName": "locations"})EOF");
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl response_body("{}");
  upstream_request_->encodeData(response_body, true);
  ASSERT_TRUE(response->waitForEndStream());

  test_server_->waitForCounter("http.config_test.dynamodb.operation.GetItem.upstream_rq_total",
                               Eq(1));
  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.dynamodb.table.locations.upstream_rq_total_2xx")
                ->value());

  // The operation and table segments of the stat names are dropped from the tag-extracted names
  // and carried as tags, identically in both stats modes.
  const std::string& hcm_prefix = Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX;
  Stats::TestUtil::expectStatTags(
      *test_server_, "http.config_test.dynamodb.operation.GetItem.upstream_rq_total",
      "http.dynamodb.operation.upstream_rq_total",
      {{hcm_prefix, "config_test"}, {Config::TagNames::get().DYNAMO_OPERATION, "GetItem"}});
  Stats::TestUtil::expectStatTags(
      *test_server_, "http.config_test.dynamodb.table.locations.upstream_rq_total",
      "http.dynamodb.table.upstream_rq_total",
      {{hcm_prefix, "config_test"}, {Config::TagNames::get().DYNAMO_TABLE, "locations"}});
  Stats::TestUtil::expectStatTags(
      *test_server_, "http.config_test.dynamodb.table.locations.upstream_rq_time",
      "http.dynamodb.table.upstream_rq_time",
      {{hcm_prefix, "config_test"}, {Config::TagNames::get().DYNAMO_TABLE, "locations"}});
}

} // namespace
} // namespace Envoy
