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
  filter->initializeInModuleFilter();

  // The followings are mostly for coverage at the moment.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  filter->setEncoderFilterCallbacks(encoder_callbacks);
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
  filter->onStreamComplete();
  filter->onDestroy();
}

TEST(DynamiModulesTest, HeaderCallbacks) {
  const std::string filter_name = "header_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()));
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  Http::TestResponseHeaderMapImpl response_headers{headers};
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->decodeTrailers(request_trailers));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));

  filter->onDestroy();
}

TEST(DynamiModulesTest, DynamicMetadataCallbacks) {
  const std::string filter_name = "dynamic_metadata_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()));
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));
  filter->setDecoderFilterCallbacks(callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};
  Http::TestResponseHeaderMapImpl response_headers{};
  Buffer::OwnedImpl data;
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data, false));

  // Check dynamic metadata set by the filter during even hooks.
  auto ns_req_header = metadata.filter_metadata().find("ns_req_header");
  ASSERT_NE(ns_req_header, metadata.filter_metadata().end());
  auto key = ns_req_header->second.fields().find("key");
  ASSERT_NE(key, ns_req_header->second.fields().end());
  EXPECT_EQ(key->second.number_value(), 123);
  auto ns_res_header = metadata.filter_metadata().find("ns_res_header");
  ASSERT_NE(ns_res_header, metadata.filter_metadata().end());
  key = ns_res_header->second.fields().find("key");
  ASSERT_NE(key, ns_res_header->second.fields().end());
  EXPECT_EQ(key->second.number_value(), 123);
  auto ns_req_body = metadata.filter_metadata().find("ns_req_body");
  ASSERT_NE(ns_req_body, metadata.filter_metadata().end());
  key = ns_req_body->second.fields().find("key");
  ASSERT_NE(key, ns_req_body->second.fields().end());
  EXPECT_EQ(key->second.string_value(), "value");
  auto ns_res_body = metadata.filter_metadata().find("ns_res_body");
  ASSERT_NE(ns_res_body, metadata.filter_metadata().end());
  key = ns_res_body->second.fields().find("key");
  ASSERT_NE(key, ns_res_body->second.fields().end());
  EXPECT_EQ(key->second.string_value(), "value");

  filter->onDestroy();
}

TEST(DynamiModulesTest, BodyCallbacks) {
  const std::string filter_name = "body_callbacks";
  const std::string filter_config = "";
  // TODO: Add non-Rust test program once we have non-Rust SDK.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("http", "rust"), false);
  if (!dynamic_module.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to load dynamic module: {}", dynamic_module.status().message());
  }
  EXPECT_TRUE(dynamic_module.ok());

  auto filter_config_or_status =
      Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
          filter_name, filter_config, std::move(dynamic_module.value()));
  EXPECT_TRUE(filter_config_or_status.ok());

  auto filter = std::make_shared<DynamicModuleHttpFilter>(filter_config_or_status.value());
  filter->initializeInModuleFilter();

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(encoder_callbacks);
  Buffer::OwnedImpl request_body;
  EXPECT_CALL(decoder_callbacks, decodingBuffer()).WillRepeatedly(testing::Return(&request_body));
  Buffer::OwnedImpl response_body;
  EXPECT_CALL(encoder_callbacks, encodingBuffer()).WillRepeatedly(testing::Return(&response_body));
  EXPECT_CALL(decoder_callbacks, modifyDecodingBuffer(_))
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(request_body);
      }));
  EXPECT_CALL(encoder_callbacks, modifyEncodingBuffer(_))
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(response_body);
      }));

  request_body.add("nice");
  filter->decodeData(request_body, false);
  EXPECT_EQ(request_body.toString(), "foo");
  request_body.drain(request_body.length());
  request_body.add("nice");
  filter->decodeData(request_body, false);
  EXPECT_EQ(request_body.toString(), "foo");
  request_body.drain(request_body.length());
  request_body.add("nice");
  filter->decodeData(request_body, true);
  EXPECT_EQ(request_body.toString(), "fooend");

  response_body.add("cool");
  filter->encodeData(response_body, false);
  EXPECT_EQ(response_body.toString(), "bar");
  response_body.drain(response_body.length());
  response_body.add("cool");
  filter->encodeData(response_body, false);
  EXPECT_EQ(response_body.toString(), "bar");
  response_body.drain(response_body.length());
  response_body.add("cool");
  filter->encodeData(response_body, true);
  EXPECT_EQ(response_body.toString(), "barend");
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
