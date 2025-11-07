#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class StatsAccessLogIntegrationTest : public HttpIntegrationTest,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  StatsAccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void init(const std::string& config_yaml) {
    autonomous_upstream_ = true;
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* access_log = hcm.add_access_log();
          TestUtility::loadFromYaml(config_yaml, *access_log);
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsAccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsAccessLogIntegrationTest, Basic) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                counters:
                  - stat:
                      name: fixedcounter
                      tags:
                        - name: fixed_tag
                          value_format: fixed_value
                        - name: dynamic_tag
                          value_format: '%REQUEST_HEADER(tag-value)%_%PROTOCOL%'
                    value_fixed: 42
                  - stat:
                      name: formatcounter
                    value_format: '%RESPONSE_CODE%'
                histograms:
                  - stat:
                      name: testhistogram
                      tags:
                        - name: tag
                          value_format: '%REQUEST_HEADER(tag-value)%'
                    value_format: '%REQUEST_HEADER(histogram-value)%'

)EOF";

  init(config_yaml);

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

// Trigger simultaneous logs on multiple workers to trigger TSAN errors if present.
TEST_P(StatsAccessLogIntegrationTest, Concurrency) {
  concurrency_ = 2;
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                counters:
                  - stat:
                      name: formatcounter
                    value_format: '%RESPONSE_CODE%'
                histograms:
                  - stat:
                      name: testhistogram
                      tags:
                        - name: tag
                          value_format: '%REQUEST_HEADER(tag-value)%'
                    value_format: '%REQUEST_HEADER(histogram-value)%'

)EOF";

  init(config_yaml);

  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},       {":authority", "envoyproxy.io"}, {":path", "/test/long/url"},
      {":scheme", "http"},      {"tag-value", "mytagvalue"},     {"counter-value", "7"},
      {"histogram-value", "2"},
  };

  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < 10; i++) {
    threads.emplace_back([&]() {
      for (uint32_t requests = 0; requests < 10; requests++) {
        BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
            lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "envoyproxy.io");
        ASSERT_TRUE(response->complete());
        EXPECT_EQ("200", response->headers().getStatusValue());
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

TEST_P(StatsAccessLogIntegrationTest, PercentHistogram) {
  const std::string config_yaml = R"EOF(
              name: envoy.access_loggers.stats
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.stats.v3.Config
                stat_prefix: test_stat_prefix
                histograms:
                  - stat:
                      name: testhistogram
                    unit: Percent
                    value_format: '%REQUEST_HEADER(histogram-value)%'

)EOF";

  init(config_yaml);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":authority", "envoyproxy.io"}, {":path", "/test/long/url"},
      {":scheme", "http"}, {"histogram-value", "0.1"},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  test_server_->waitUntilHistogramHasSamples("test_stat_prefix.testhistogram");

  auto histogram = test_server_->histogram("test_stat_prefix.testhistogram");
  EXPECT_EQ(1, TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram));

  double p100 = histogram->cumulativeStatistics().computedQuantiles().back();
  EXPECT_NEAR(0.1, p100, 0.05);
}

} // namespace
} // namespace Envoy
