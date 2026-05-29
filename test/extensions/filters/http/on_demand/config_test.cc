#include "source/extensions/filters/http/on_demand/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OnDemand {
namespace {

TEST(OnDemandFilterConfigTest, OnDemandFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  OnDemandFilterFactory factory;
  envoy::extensions::filters::http::on_demand::v3::OnDemand config;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(OnDemandFilterConfigTest, OnDemandFilterWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  OnDemandFilterFactory factory;
  envoy::extensions::filters::http::on_demand::v3::OnDemand config;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config, "stats", context);
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace OnDemand
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
