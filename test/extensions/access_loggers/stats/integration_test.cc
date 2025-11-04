#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class StatsAccessLogIntegrationTest : public HttpIntegrationTest,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  StatsAccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("stats_accesslog");

          envoy::extensions::access_loggers::stats::v3::Config config;
          config.set_stat_prefix("test_stat_prefix");

          {
            auto* counter = config.add_counters();
            counter->mutable_stat()->set_name("fixedcounter");
            counter->mutable_value_fixed()->set_value(42);
            auto* tag1 = counter->mutable_stat()->add_tags();
            tag1->set_name("fixed_tag");
            tag1->set_value_format("fixed_value");
            auto* tag2 = counter->mutable_stat()->add_tags();
            tag2->set_name("dynamic_tag");
            tag2->set_value_format("%REQUEST_HEADER(tag-value)%_%PROTOCOL%");
          }
          {
            auto* counter = config.add_counters();
            counter->mutable_stat()->set_name("formatcounter");
            counter->set_value_format("%RESPONSE_CODE%");
          }

          {
            auto* histogram = config.add_histograms();
            histogram->mutable_stat()->set_name("testhistogram");
            histogram->set_value_format("%REQUEST_HEADER(histogram-value)%");
            auto* tag = histogram->mutable_stat()->add_tags();
            tag->set_name("tag");
            tag->set_value_format("%REQUEST_HEADER(tag-value)%");
          }
          access_log->mutable_typed_config()->PackFrom(config);
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsAccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsAccessLogIntegrationTest, Basic) {
  autonomous_upstream_ = true;
  initialize();

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},       {":authority", "envoyproxy.io"}, {":path", "/test/long/url"},
      {":scheme", "http"},      {"tag-value", "mytagvalue"},     {"counter-value", "7"},
      {"histogram-value", "2"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  test_server_->waitForCounterEq(
      "test_stat_prefix.fixedcounter.fixed_tag.fixed_value.dynamic_tag.mytagvalue_HTTP/1.1", 42);
  test_server_->waitForCounterEq("test_stat_prefix.formatcounter", 200);
  test_server_->waitUntilHistogramHasSamples("test_stat_prefix.testhistogram.tag.mytagvalue");

  auto histogram = test_server_->histogram("test_stat_prefix.testhistogram.tag.mytagvalue");
  EXPECT_EQ(1, TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram));
  EXPECT_EQ(2, static_cast<uint32_t>(
                   TestUtility::readSampleSum(test_server_->server().dispatcher(), *histogram)));
}

} // namespace
} // namespace Envoy
