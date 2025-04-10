#include "source/common/router/string_accessor_impl.h"
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

TEST(DynamicModulesTest, HeaderCallbacks) {
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

  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  Http::MockDownstreamStreamFilterCallbacks downstream_callbacks;
  EXPECT_CALL(downstream_callbacks, clearRouteCache());
  EXPECT_CALL(callbacks, downstreamCallbacks())
      .WillOnce(testing::Return(OptRef(downstream_callbacks)));
  filter->setDecoderFilterCallbacks(callbacks);

  NiceMock<StreamInfo::MockStreamInfo> info;
  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .WillRepeatedly(testing::ReturnPointee(info.downstream_connection_info_provider_));
  auto addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.1.1.1", 1234, false);
  info.downstream_connection_info_provider_->setRemoteAddress(addr);

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}, {"to-be-deleted", "value"}};
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

TEST(DynamicModulesTest, DynamicMetadataCallbacks) {
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

TEST(DynamicModulesTest, FilterStateCallbacks) {
  const std::string filter_name = "filter_state_callbacks";
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
  EXPECT_CALL(stream_info, filterState())
      .WillRepeatedly(testing::ReturnRef(stream_info.filter_state_));
  filter->setDecoderFilterCallbacks(callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};
  Http::TestRequestTrailerMapImpl request_trailers{};
  Http::TestResponseHeaderMapImpl response_headers{};
  Http::TestResponseTrailerMapImpl response_trailers{};
  Buffer::OwnedImpl data;
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->decodeData(data, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->decodeTrailers(request_trailers));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter->encodeHeaders(response_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter->encodeData(data, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter->encodeTrailers(response_trailers));

  // Check filter state set by the filter during even hooks.
  const auto* req_header_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("req_header_key");
  ASSERT_NE(req_header_value, nullptr);
  EXPECT_EQ(req_header_value->serializeAsString(), "req_header_value");
  const auto* req_body_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("req_body_key");
  ASSERT_NE(req_body_value, nullptr);
  EXPECT_EQ(req_body_value->serializeAsString(), "req_body_value");
  const auto* req_trailer_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("req_trailer_key");
  ASSERT_NE(req_trailer_value, nullptr);
  EXPECT_EQ(req_trailer_value->serializeAsString(), "req_trailer_value");
  const auto* res_header_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("res_header_key");
  ASSERT_NE(res_header_value, nullptr);
  EXPECT_EQ(res_header_value->serializeAsString(), "res_header_value");
  const auto* res_body_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("res_body_key");
  ASSERT_NE(res_body_value, nullptr);
  EXPECT_EQ(res_body_value->serializeAsString(), "res_body_value");
  const auto* res_trailer_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("res_trailer_key");
  ASSERT_NE(res_trailer_value, nullptr);
  EXPECT_EQ(res_trailer_value->serializeAsString(), "res_trailer_value");
  // There is no filter state named key set by the filter.
  const auto* value = stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("key");
  ASSERT_EQ(value, nullptr);

  filter->onStreamComplete();
  const auto* stream_complete_value =
      stream_info.filterState()->getDataReadOnly<Router::StringAccessor>("stream_complete_key");
  ASSERT_NE(stream_complete_value, nullptr);
  EXPECT_EQ(stream_complete_value->serializeAsString(), "stream_complete_value");
}

TEST(DynamicModulesTest, BodyCallbacks) {
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
  EXPECT_CALL(decoder_callbacks, addDecodedData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> void {}));
  Buffer::OwnedImpl response_body;
  EXPECT_CALL(encoder_callbacks, encodingBuffer()).WillRepeatedly(testing::Return(&response_body));
  EXPECT_CALL(encoder_callbacks, addEncodedData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> void {}));
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
