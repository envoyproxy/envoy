#include "source/extensions/filters/http/a2a/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {
namespace {

using testing::_;
using testing::NiceMock;

class A2aFilterConfigFactoryTest : public testing::Test {
protected:
  A2aFilterConfigFactory factory_;
};

TEST_F(A2aFilterConfigFactoryTest, CreateFilterFactory) {
  envoy::extensions::filters::http::a2a::v3::A2a config;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  // Envoy OSS uses absl::StatusOr, so we check .ok() or .status().ok()
  auto cb = factory_.createFilterFactoryFromProto(config, "stats", context);
  EXPECT_OK(cb.status());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb.value()(filter_callback);
}

TEST_F(A2aFilterConfigFactoryTest, CreateFilterWithServerContext) {
  envoy::extensions::filters::http::a2a::v3::A2a config;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  Http::FilterFactoryCb cb =
      factory_.createHttpFilterFactoryFromProto(config, "stats", server_context).value();

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
