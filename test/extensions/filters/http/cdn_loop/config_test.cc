#include <string>

#include "envoy/extensions/filters/http/cdn_loop/v3/cdn_loop.pb.h"

#include "source/extensions/filters/http/cdn_loop/config.h"
#include "source/extensions/filters/http/cdn_loop/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

using testing::HasSubstr;

TEST(CdnLoopFilterFactoryTest, ValidValuesWork) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::StreamDecoderFilterSharedPtr filter;
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));

  envoy::extensions::filters::http::cdn_loop::v3::CdnLoopConfig config;
  config.set_cdn_id("cdn");
  CdnLoopFilterFactory factory;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  cb(filter_callbacks);
  EXPECT_NE(filter.get(), nullptr);
  EXPECT_NE(dynamic_cast<CdnLoopFilter*>(filter.get()), nullptr);
}

TEST(CdnLoopFilterFactoryTest, BlankCdnIdThrows) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::http::cdn_loop::v3::CdnLoopConfig config;
  CdnLoopFilterFactory factory;

  EXPECT_THAT_THROWS_MESSAGE(factory.createFilterFactoryFromProto(config, "stats", context).value(),
                             ProtoValidationException, HasSubstr("value length must be at least"));
}

TEST(CdnLoopFilterFactoryTest, InvalidCdnId) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::http::cdn_loop::v3::CdnLoopConfig config;
  config.set_cdn_id("[not-token-or-ip");
  CdnLoopFilterFactory factory;

  EXPECT_THAT_THROWS_MESSAGE(factory.createFilterFactoryFromProto(config, "stats", context).value(),
                             EnvoyException, HasSubstr("is not a valid CDN identifier"));
}

TEST(CdnLoopFilterFactoryTest, InvalidCdnIdNonHeaderWhitespace) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::http::cdn_loop::v3::CdnLoopConfig config;
  config.set_cdn_id("\r\n");
  CdnLoopFilterFactory factory;

  EXPECT_THAT_THROWS_MESSAGE(factory.createFilterFactoryFromProto(config, "stats", context).value(),
                             EnvoyException, HasSubstr("is not a valid CDN identifier"));
}

TEST(CdnLoopFilterFactoryTest, InvalidParsedCdnIdNotInput) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::http::cdn_loop::v3::CdnLoopConfig config;
  config.set_cdn_id("cdn,cdn");
  CdnLoopFilterFactory factory;

  EXPECT_THAT_THROWS_MESSAGE(factory.createFilterFactoryFromProto(config, "stats", context).value(),
                             EnvoyException, HasSubstr("is not a valid CDN identifier"));
}

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
