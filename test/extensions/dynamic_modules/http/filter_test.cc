#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

INSTANTIATE_TEST_SUITE_P(LanguageTests, DynamicModuleTestLanguages, testing::Values("c", "rust"),
                         DynamicModuleTestLanguages::languageParamToTestName);

TEST_P(DynamicModuleTestLanguages, Nop) {
  const std::string filter_name = "foo";
  const std::string filter_config = "bar";

  const auto language = GetParam();
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(dynamic_module.ok());

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()));
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());

  // The followings are mostly for coverage at the moment.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);
  filter->onStreamComplete();
  filter->onDestroy();
  TestRequestHeaderMapImpl headers{{}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(headers, false));
  Buffer::OwnedImpl data;
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data, false));
  TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->decodeTrailers(trailers));
  MetadataMap metadata;
  EXPECT_EQ(FilterMetadataStatus::Continue, filter->decodeMetadata(metadata));
  filter->decodeComplete();
  TestResponseHeaderMapImpl response_headers{{}};
  EXPECT_EQ(Filter1xxHeadersStatus::Continue, filter->encode1xxHeaders(response_headers));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data, false));
  TestResponseTrailerMapImpl response_trailers;
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));
  EXPECT_EQ(FilterMetadataStatus::Continue, filter->encodeMetadata(metadata));
  filter->encodeComplete();
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
