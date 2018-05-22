#pragma once

#include "common/json/json_loader.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class IntegrationAdminTest : public HttpIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  IntegrationAdminTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
    HttpIntegrationTest::initialize();
  }

  /**
   *  Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  /**
   * Validates that the passed in string conforms to output of stats in JSON format.
   */
  void validateStatsJson(const std::string stats_json) {
    Json::ObjectSharedPtr statsjson = Json::Factory::loadFromString(stats_json);
    EXPECT_TRUE(statsjson->hasObject("stats"));
    uint64_t histogram_count = 0;
    for (Json::ObjectSharedPtr obj_ptr : statsjson->getObjectArray("stats")) {
      if (obj_ptr->hasObject("histograms")) {
        histogram_count++;
        const Json::ObjectSharedPtr& histograms_ptr = obj_ptr->getObject("histograms");
        // Validate that both supported_quantiles and computed_quantiles are present in JSON.
        EXPECT_TRUE(histograms_ptr->hasObject("supported_quantiles"));
        EXPECT_TRUE(histograms_ptr->hasObject("computed_quantiles"));

        const std::vector<Json::ObjectSharedPtr>& computed_quantiles =
            histograms_ptr->getObjectArray("computed_quantiles");
        EXPECT_GT(computed_quantiles.size(), 0);

        // Validate that each computed_quantile has name and value objects.
        EXPECT_TRUE(computed_quantiles[0]->hasObject("name"));
        EXPECT_TRUE(computed_quantiles[0]->hasObject("values"));

        // Validate that supported and computed quantiles are of the same size.
        EXPECT_EQ(histograms_ptr->getObjectArray("supported_quantiles").size(),
                  computed_quantiles[0]->getObjectArray("values").size());
      }
    }

    // Validate that the stats JSON has exactly one histograms element.
    EXPECT_EQ(1, histogram_count);
  }
};

} // namespace Envoy
