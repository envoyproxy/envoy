#include "envoy/api/v2/filter/http/router.pb.h"

#include "common/config/filter_json.h"
#include "common/json/json_loader.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

envoy::api::v2::filter::http::Router parseRouterFromJson(const std::string& json_string) {
  envoy::api::v2::filter::http::Router router;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::FilterJson::translateRouter(*json_object_ptr, router);
  return router;
}

} // namespace

TEST(FilterJsonTest, TranslateRouter) {
  std::string json_string = R"EOF(
    {
      "dynamic_stats": false,
      "start_child_span": true
    }
  )EOF";

  auto router = parseRouterFromJson(json_string);
  EXPECT_FALSE(router.dynamic_stats().value());
  EXPECT_TRUE(router.start_child_span());
}

TEST(FilterJsonTest, TranslateRouterDefaults) {
  std::string json_string = "{}";
  auto router = parseRouterFromJson(json_string);
  EXPECT_TRUE(router.dynamic_stats().value());
  EXPECT_FALSE(router.start_child_span());
}

} // namespace Config
} // namespace Envoy
