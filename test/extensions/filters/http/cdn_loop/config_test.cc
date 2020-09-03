#include "envoy/extensions/filters/http/cdn_loop/v3alpha/cdn_loop.pb.h"

#include "extensions/filters/http/cdn_loop/config.h"
#include "extensions/filters/http/cdn_loop/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

TEST(CdnLoopFilterFactoryTest, ValidValuesWork) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::StreamDecoderFilterSharedPtr filter;
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter).WillOnce(::testing::SaveArg<0>(&filter));

  envoy::extensions::filters::http::cdn_loop::v3alpha::CdnLoopConfig config;
  config.set_cdn_id("cdn");
  CdnLoopFilterFactory factory;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  cb(filter_callbacks);
  EXPECT_NE(filter.get(), nullptr);
  EXPECT_NE(dynamic_cast<CdnLoopFilter*>(filter.get()), nullptr);
}

TEST(CdnLoopFilterFactoryTest, BlankCdnIdThrows) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::http::cdn_loop::v3alpha::CdnLoopConfig config;
  CdnLoopFilterFactory factory;

  EXPECT_THROW(factory.createFilterFactoryFromProto(config, "stats", context), EnvoyException);
}

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
