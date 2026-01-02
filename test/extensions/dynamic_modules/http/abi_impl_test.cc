#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>

#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

class DynamicModuleHttpFilterTest : public testing::Test {
public:
  void SetUp() override {
    filter_ = std::make_unique<DynamicModuleHttpFilter>(nullptr, symbol_table_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  Stats::SymbolTableImpl symbol_table_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  std::unique_ptr<DynamicModuleHttpFilter> filter_;
};

class DynamicModuleHttpFilterHeaderTest
    : public DynamicModuleHttpFilterTest,
      public ::testing::WithParamInterface<envoy_dynamic_module_type_http_header_type> {};

INSTANTIATE_TEST_SUITE_P(
    HeaderTest, DynamicModuleHttpFilterHeaderTest,
    ::testing::Values(envoy_dynamic_module_type_http_header_type_RequestHeader,
                      envoy_dynamic_module_type_http_header_type_RequestTrailer,
                      envoy_dynamic_module_type_http_header_type_ResponseHeader,
                      envoy_dynamic_module_type_http_header_type_ResponseTrailer));

TEST_P(DynamicModuleHttpFilterHeaderTest, GetHeaderValue) {
  envoy_dynamic_module_type_http_header_type header_type = GetParam();

  // Test with nullptr accessors.
  envoy_dynamic_module_type_envoy_buffer result_buffer{nullptr, 0};
  size_t index = 0;
  size_t optional_size = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_header(
      filter_.get(), header_type, {nullptr, 0}, &result_buffer, index, &optional_size));
  EXPECT_EQ(result_buffer.ptr, nullptr);
  EXPECT_EQ(result_buffer.length, 0);
  EXPECT_EQ(optional_size, 0);

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  Http::TestResponseHeaderMapImpl response_headers{headers};
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));
  EXPECT_CALL(decoder_callbacks_, requestTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<RequestTrailerMap>(request_trailers)));
  EXPECT_CALL(encoder_callbacks_, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));
  EXPECT_CALL(encoder_callbacks_, responseTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseTrailerMap>(response_trailers)));

  // The key is not found.
  std::string key = "nonexistent";
  result_buffer = {nullptr, 0};
  index = 0;
  optional_size = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_header(
      filter_.get(), header_type, {key.data(), key.size()}, &result_buffer, index, &optional_size));
  EXPECT_EQ(optional_size, 0);
  EXPECT_EQ(result_buffer.ptr, nullptr);
  EXPECT_EQ(result_buffer.length, 0);
  // The key is found for single value.
  key = "single";

  result_buffer = {nullptr, 0};
  index = 0;
  optional_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_header(
      filter_.get(), header_type, {key.data(), key.size()}, &result_buffer, index, &optional_size));
  EXPECT_EQ(optional_size, 1);
  EXPECT_NE(result_buffer.ptr, nullptr);
  EXPECT_EQ(result_buffer.length, 5);
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "value");
  // key is found for the single value but index is out of range.
  result_buffer = {nullptr, 0};
  index = 1;
  optional_size = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_header(
      filter_.get(), header_type, {key.data(), key.size()}, &result_buffer, index, &optional_size));
  EXPECT_EQ(optional_size, 1);
  EXPECT_EQ(result_buffer.ptr, nullptr);
  EXPECT_EQ(result_buffer.length, 0);

  // The key is found for multiple values.
  key = "multi";

  result_buffer = {nullptr, 0};
  index = 0;
  optional_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_header(
      filter_.get(), header_type, {key.data(), key.size()}, &result_buffer, index, &optional_size));
  EXPECT_EQ(optional_size, 2);
  EXPECT_NE(result_buffer.ptr, nullptr);
  EXPECT_EQ(result_buffer.length, 6);
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "value1");
  result_buffer = {nullptr, 0};
  index = 1;
  optional_size = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_header(
      filter_.get(), header_type, {key.data(), key.size()}, &result_buffer, index, &optional_size));
  EXPECT_EQ(optional_size, 2);
  EXPECT_NE(result_buffer.ptr, nullptr);
  EXPECT_EQ(result_buffer.length, 6);
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "value2");
  // The key is found for multiple values but index is out of range.
  result_buffer = {nullptr, 0};
  index = 2;
  optional_size = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_header(
      filter_.get(), header_type, {key.data(), key.size()}, &result_buffer, index, &optional_size));
  EXPECT_EQ(optional_size, 2);
  EXPECT_EQ(result_buffer.ptr, nullptr);
  EXPECT_EQ(result_buffer.length, 0);
}

TEST_P(DynamicModuleHttpFilterHeaderTest, AddHeaderValue) {
  envoy_dynamic_module_type_http_header_type header_type = GetParam();

  // Test with nullptr accessors.
  const std::string key = "key";
  const std::string value = "value";

  EXPECT_FALSE(envoy_dynamic_module_callback_http_add_header(
      filter_.get(), header_type, {key.data(), key.size()}, {value.data(), value.size()}));

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  Http::TestResponseHeaderMapImpl response_headers{headers};
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));
  EXPECT_CALL(decoder_callbacks_, requestTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<RequestTrailerMap>(request_trailers)));
  EXPECT_CALL(encoder_callbacks_, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));
  EXPECT_CALL(encoder_callbacks_, responseTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseTrailerMap>(response_trailers)));

  Http::HeaderMap* header_map = nullptr;
  if (header_type == envoy_dynamic_module_type_http_header_type_RequestHeader) {
    header_map = &request_headers;
  } else if (header_type == envoy_dynamic_module_type_http_header_type_RequestTrailer) {
    header_map = &request_trailers;
  } else if (header_type == envoy_dynamic_module_type_http_header_type_ResponseHeader) {
    header_map = &response_headers;
  } else if (header_type == envoy_dynamic_module_type_http_header_type_ResponseTrailer) {
    header_map = &response_trailers;
  } else {
    FAIL();
  }

  // Non existing key.
  const std::string new_key = "new_one";
  const std::string new_value = "value";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_add_header(filter_.get(), header_type,
                                                            {new_key.data(), new_key.size()},
                                                            {new_value.data(), new_value.size()}));

  auto values = header_map->get(Envoy::Http::LowerCaseString(new_key));
  EXPECT_EQ(values.size(), 1);
  EXPECT_EQ(values[0]->value().getStringView(), new_value);

  // Existing non-multi key.
  const std::string key2 = "single";
  const std::string value2 = "new_value";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_add_header(
      filter_.get(), header_type, {key2.data(), key2.size()}, {value2.data(), value2.size()}));

  auto values2 = header_map->get(Envoy::Http::LowerCaseString(key2));
  EXPECT_EQ(values2.size(), 2);
  EXPECT_EQ(values2[0]->value().getStringView(), "value");
  EXPECT_EQ(values2[1]->value().getStringView(), value2);

  // Existing multi key must be replaced by a single value.
  const std::string key3 = "multi";
  const std::string value3 = "new_value";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_add_header(
      filter_.get(), header_type, {key3.data(), key3.size()}, {value3.data(), value3.size()}));

  auto values3 = header_map->get(Envoy::Http::LowerCaseString(key3));
  EXPECT_EQ(values3.size(), 3);
  EXPECT_EQ(values3[0]->value().getStringView(), "value1");
  EXPECT_EQ(values3[1]->value().getStringView(), "value2");
  EXPECT_EQ(values3[2]->value().getStringView(), value3);
}

TEST_P(DynamicModuleHttpFilterHeaderTest, SetHeaderValue) {
  envoy_dynamic_module_type_http_header_type header_type = GetParam();

  // Test with nullptr accessors.
  const std::string key = "key";
  const std::string value = "value";
  EXPECT_FALSE(envoy_dynamic_module_callback_http_set_header(
      filter_.get(), header_type, {key.data(), key.size()}, {value.data(), value.size()}));

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  Http::TestResponseHeaderMapImpl response_headers{headers};
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));
  EXPECT_CALL(decoder_callbacks_, requestTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<RequestTrailerMap>(request_trailers)));
  EXPECT_CALL(encoder_callbacks_, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));
  EXPECT_CALL(encoder_callbacks_, responseTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseTrailerMap>(response_trailers)));

  Http::HeaderMap* header_map = nullptr;
  if (header_type == envoy_dynamic_module_type_http_header_type_RequestHeader) {
    header_map = &request_headers;
  } else if (header_type == envoy_dynamic_module_type_http_header_type_RequestTrailer) {
    header_map = &request_trailers;
  } else if (header_type == envoy_dynamic_module_type_http_header_type_ResponseHeader) {
    header_map = &response_headers;
  } else if (header_type == envoy_dynamic_module_type_http_header_type_ResponseTrailer) {
    header_map = &response_trailers;
  } else {
    FAIL();
  }

  // Non existing key.
  const std::string new_key = "new_one";
  const std::string new_value = "value";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_header(filter_.get(), header_type,
                                                            {new_key.data(), new_key.size()},
                                                            {new_value.data(), new_value.size()}));

  auto values = header_map->get(Envoy::Http::LowerCaseString(new_key));
  EXPECT_EQ(values.size(), 1);
  EXPECT_EQ(values[0]->value().getStringView(), new_value);

  // Existing non-multi key.
  const std::string key2 = "single";
  const std::string value2 = "new_value";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_header(
      filter_.get(), header_type, {key2.data(), key2.size()}, {value2.data(), value2.size()}));

  auto values2 = header_map->get(Envoy::Http::LowerCaseString(key2));
  EXPECT_EQ(values2.size(), 1);
  EXPECT_EQ(values2[0]->value().getStringView(), value2);

  // Existing multi key must be replaced by a single value.
  const std::string key3 = "multi";
  const std::string value3 = "new_value";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_header(
      filter_.get(), header_type, {key3.data(), key3.size()}, {value3.data(), value3.size()}));

  auto values3 = header_map->get(Envoy::Http::LowerCaseString(key3));
  EXPECT_EQ(values3.size(), 1);
  EXPECT_EQ(values3[0]->value().getStringView(), value3);

  // Remove the key by passing null value.
  const std::string remove_key = "single";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_header(
      filter_.get(), header_type, {remove_key.data(), remove_key.size()}, {nullptr, 0}));
  auto removed_values = header_map->get(Envoy::Http::LowerCaseString(remove_key));
  EXPECT_EQ(removed_values.size(), 0);
}

TEST_P(DynamicModuleHttpFilterHeaderTest, GetHeadersCount) {
  envoy_dynamic_module_type_http_header_type header_type = GetParam();

  size_t size = 0;

  // Test with nullptr accessors.
  EXPECT_EQ(envoy_dynamic_module_callback_http_get_headers_size(filter_.get(), header_type, &size),
            false);

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  Http::TestResponseHeaderMapImpl response_headers{headers};
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));
  EXPECT_CALL(decoder_callbacks_, requestTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<RequestTrailerMap>(request_trailers)));
  EXPECT_CALL(encoder_callbacks_, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));
  EXPECT_CALL(encoder_callbacks_, responseTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseTrailerMap>(response_trailers)));

  EXPECT_TRUE(
      envoy_dynamic_module_callback_http_get_headers_size(filter_.get(), header_type, &size));
  EXPECT_EQ(size, 3);
}

TEST_P(DynamicModuleHttpFilterHeaderTest, GetHeaders) {
  envoy_dynamic_module_type_http_header_type header_type = GetParam();

  // Test with nullptr accessors.
  envoy_dynamic_module_type_envoy_http_header result_headers[3];
  EXPECT_FALSE(
      envoy_dynamic_module_callback_http_get_headers(filter_.get(), header_type, result_headers));
  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  EXPECT_CALL(decoder_callbacks_, requestTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<RequestTrailerMap>(request_trailers)));
  Http::TestResponseHeaderMapImpl response_headers{headers};
  EXPECT_CALL(encoder_callbacks_, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  EXPECT_CALL(encoder_callbacks_, responseTrailers())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseTrailerMap>(response_trailers)));

  EXPECT_TRUE(
      envoy_dynamic_module_callback_http_get_headers(filter_.get(), header_type, result_headers));

  EXPECT_EQ(result_headers[0].key_length, 6);
  EXPECT_EQ(std::string(result_headers[0].key_ptr, result_headers[0].key_length), "single");
  EXPECT_EQ(result_headers[0].value_length, 5);
  EXPECT_EQ(std::string(result_headers[0].value_ptr, result_headers[0].value_length), "value");

  EXPECT_EQ(result_headers[1].key_length, 5);
  EXPECT_EQ(std::string(result_headers[1].key_ptr, result_headers[1].key_length), "multi");
  EXPECT_EQ(result_headers[1].value_length, 6);
  EXPECT_EQ(std::string(result_headers[1].value_ptr, result_headers[1].value_length), "value1");

  EXPECT_EQ(result_headers[2].key_length, 5);
  EXPECT_EQ(std::string(result_headers[2].key_ptr, result_headers[2].key_length), "multi");
  EXPECT_EQ(result_headers[2].value_length, 6);
  EXPECT_EQ(std::string(result_headers[2].value_ptr, result_headers[2].value_length), "value2");
}

TEST_F(DynamicModuleHttpFilterTest, SendResponseNullptr) {
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq(""), _,
                                                 testing::Eq(0), testing::Eq("dynamic_module")));
  envoy_dynamic_module_callback_http_send_response(filter_.get(), 200, nullptr, 0, {nullptr, 0},
                                                   {nullptr, 0});
}

TEST_F(DynamicModuleHttpFilterTest, SendResponseEmptyResponse) {
  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_CALL(encoder_callbacks_, responseHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<ResponseHeaderMap>(response_headers)));

  // Test with empty response.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq(""), _,
                                                 testing::Eq(0), testing::Eq("dynamic_module")));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));

  envoy_dynamic_module_callback_http_send_response(filter_.get(), 200, nullptr, 0, {nullptr, 0},
                                                   {nullptr, 0});
}

TEST_F(DynamicModuleHttpFilterTest, SendResponse) {
  std::list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  size_t header_count = headers.size();
  auto header_array =
      std::make_unique<envoy_dynamic_module_type_module_http_header[]>(header_count);

  size_t index = 0;
  for (const auto& [key, value] : headers) {
    header_array[index].key_length = key.size();
    header_array[index].key_ptr = const_cast<char*>(key.c_str());
    header_array[index].value_length = value.size();
    header_array[index].value_ptr = const_cast<char*>(value.c_str());
    ++index;
  }
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq(""), _,
                                                 testing::Eq(0), testing::Eq("dynamic_module")));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _)).WillOnce(Invoke([](auto& headers, auto) {
    EXPECT_EQ(headers.get(Http::LowerCaseString("single"))[0]->value().getStringView(), "value");
    EXPECT_EQ(headers.get(Http::LowerCaseString("multi"))[0]->value().getStringView(), "value1");
    EXPECT_EQ(headers.get(Http::LowerCaseString("multi"))[1]->value().getStringView(), "value2");
  }));

  envoy_dynamic_module_callback_http_send_response(filter_.get(), 200, header_array.get(),
                                                   header_count, {nullptr, 0}, {nullptr, 0});
}

TEST_F(DynamicModuleHttpFilterTest, SendResponseWithBody) {
  std::list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  size_t header_count = headers.size();
  auto header_array =
      std::make_unique<envoy_dynamic_module_type_module_http_header[]>(header_count);

  size_t index = 0;
  for (const auto& [key, value] : headers) {
    header_array[index].key_length = key.size();
    header_array[index].key_ptr = const_cast<char*>(key.c_str());
    header_array[index].value_length = value.size();
    header_array[index].value_ptr = const_cast<char*>(value.c_str());
    ++index;
  }

  const std::string body_str = "body";
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq("body"), _,
                                                 testing::Eq(0), testing::Eq("dynamic_module")));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _)).WillOnce(Invoke([](auto& headers, auto) {
    EXPECT_EQ(headers.get(Http::LowerCaseString("single"))[0]->value().getStringView(), "value");
    EXPECT_EQ(headers.get(Http::LowerCaseString("multi"))[0]->value().getStringView(), "value1");
    EXPECT_EQ(headers.get(Http::LowerCaseString("multi"))[1]->value().getStringView(), "value2");
  }));
  envoy_dynamic_module_callback_http_send_response(
      filter_.get(), 200, header_array.get(), 3, {body_str.data(), body_str.size()}, {nullptr, 0});
}

TEST_F(DynamicModuleHttpFilterTest, SendResponseWithCustomResponseCodeDetails) {
  const std::string body_str = "body";
  absl::string_view test_details = "test_details";
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq("body"), _,
                                                 testing::Eq(0), testing::Eq("test_details")));
  envoy_dynamic_module_callback_http_send_response(filter_.get(), 200, nullptr, 0,
                                                   {body_str.data(), body_str.size()},
                                                   {test_details.data(), test_details.size()});
}

TEST_F(DynamicModuleHttpFilterTest, AddCustomFlag) {
  // Test with empty response.
  EXPECT_CALL(decoder_callbacks_.stream_info_, addCustomFlag(testing::Eq("XXX")));
  absl::string_view flag = "XXX";
  envoy_dynamic_module_callback_http_add_custom_flag(filter_.get(), {flag.data(), flag.size()});
}

TEST(ABIImpl, metadata) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  const std::string namespace_str = "foo";
  const std::string key_str = "key";
  double value = 42;
  const std::string value_str = "value";

  double result_number = 0;
  envoy_dynamic_module_type_envoy_buffer result_buffer = {nullptr, 0};

  // No stream info.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
      &filter, {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      value));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
      &filter, {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      {value_str.data(), value_str.size()}));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_number(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_number));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_buffer));

  // No namespace.
  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(callbacks, clusterInfo()).WillRepeatedly(testing::Return(nullptr));
  EXPECT_CALL(stream_info, route()).WillRepeatedly(testing::Return(nullptr));
  EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(testing::Return(nullptr));
  EXPECT_CALL(testing::Const(stream_info), dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  filter.setDecoderFilterCallbacks(callbacks);
  // Only tests get methods as setters create the namespace.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_number(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_number));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_buffer));
  // Test no metadata on all sources.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Cluster,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_buffer));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Route,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_buffer));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Host,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_buffer));

  // With namespace but non existing key.
  const std::string non_existing_key = "non_existing";
  // This will create the namespace.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
      &filter, {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      value));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_number(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()},
      {non_existing_key.data(), non_existing_key.size()}, &result_number));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()},
      {non_existing_key.data(), non_existing_key.size()}, &result_buffer));

  // With namespace and key.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
      &filter, {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      value));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_metadata_number(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_number));
  EXPECT_EQ(result_number, value);
  // Wrong type.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_buffer));

  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
      &filter, {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      {value_str.data(), value_str.size()}));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_buffer));
  EXPECT_EQ(absl::string_view(result_buffer.ptr, result_buffer.length), value_str);
  // Wrong type.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_metadata_number(
      &filter, envoy_dynamic_module_type_metadata_source_Dynamic,
      {namespace_str.data(), namespace_str.size()}, {key_str.data(), key_str.size()},
      &result_number));

  // lbEndpoints metadata.
  const std::string lbendpoint_key = "lbendpoint_key";
  const std::string lbendpoint_value = "lbendpoint_value";
  auto upstream_info = std::make_shared<StreamInfo::MockUpstreamInfo>();
  auto upstream_host = std::make_shared<Upstream::MockHostDescription>();
  EXPECT_CALL(*upstream_info, upstreamHost).WillRepeatedly(testing::Return(upstream_host));
  auto locality_metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  locality_metadata->mutable_filter_metadata()->insert({namespace_str, Protobuf::Struct()});
  Protobuf::Value lbendpoint_value_proto;
  lbendpoint_value_proto.set_string_value(lbendpoint_value);
  locality_metadata->mutable_filter_metadata()
      ->at(namespace_str)
      .mutable_fields()
      ->insert({lbendpoint_key, lbendpoint_value_proto});
  EXPECT_CALL(*upstream_host, localityMetadata())
      .WillRepeatedly(testing::Return(locality_metadata));
  EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(testing::Return(upstream_info));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_metadata_string(
      &filter, envoy_dynamic_module_type_metadata_source_HostLocality,
      {namespace_str.data(), namespace_str.size()}, {lbendpoint_key.data(), lbendpoint_key.size()},
      &result_buffer));
  EXPECT_EQ(absl::string_view(result_buffer.ptr, result_buffer.length), lbendpoint_value);
}

TEST(ABIImpl, filter_state) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  const std::string key_str = "key";
  const std::string value_str = "value";

  envoy_dynamic_module_type_envoy_buffer result_buffer = {nullptr, 0};

  // No stream info.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_set_filter_state_bytes(
      &filter, {key_str.data(), key_str.size()}, {value_str.data(), value_str.size()}));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_filter_state_bytes(
      &filter, {key_str.data(), key_str.size()}, &result_buffer));

  // With stream info but non existing key.
  const std::string non_existing_key = "non_existing";
  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, filterState())
      .WillRepeatedly(testing::ReturnRef(stream_info.filter_state_));
  filter.setDecoderFilterCallbacks(callbacks);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_filter_state_bytes(
      &filter, {key_str.data(), key_str.size()}, {value_str.data(), value_str.size()}));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_filter_state_bytes(
      &filter, {non_existing_key.data(), non_existing_key.size()}, &result_buffer));

  // With key.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_filter_state_bytes(
      &filter, {key_str.data(), key_str.size()}, &result_buffer));
  EXPECT_EQ(result_buffer.length, value_str.size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), value_str);
}

std::string
bufferVectorToString(const std::vector<envoy_dynamic_module_type_envoy_buffer>& buffer_vector) {
  std::string result;
  for (const auto& buffer : buffer_vector) {
    result.append(buffer.ptr, buffer.length);
  }
  return result;
}

TEST(ABIImpl, RequestBody) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  filter.setDecoderFilterCallbacks(callbacks);

  size_t chunks_size = 0;
  size_t body_size = 0;

  // Non existing buffer should return false.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &chunks_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &body_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, {nullptr, 0}));

  Buffer::OwnedImpl buffer;
  filter.current_request_body_ = &buffer;

  // Empty buffer should return size 0 and drain should return work without problems.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &chunks_size));
  EXPECT_EQ(chunks_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &body_size));
  EXPECT_EQ(body_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, 0));

  // Append data to the buffer.
  const std::string data = "foo";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody,
      {data.data(), data.size()}));
  EXPECT_EQ(buffer.toString(), data);

  // Get the data from the buffer.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &chunks_size));
  auto result_buffer_vector = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody,
      result_buffer_vector.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector), data);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &body_size));
  EXPECT_EQ(body_size, 3);

  // Add more data to the buffer.
  const std::string data2 = "bar";
  const std::string data3 = "baz";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody,
      {data2.data(), data2.size()}));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody,
      {data3.data(), data3.size()}));
  EXPECT_EQ(buffer.toString(), data + data2 + data3);

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &chunks_size));
  auto result_buffer_vector2 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody,
      result_buffer_vector2.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector2), data + data2 + data3);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &body_size));
  EXPECT_EQ(body_size, 9);

  // Drain the first 5 bytes.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, 5));

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &chunks_size));
  auto result_buffer_vector3 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody,
      result_buffer_vector3.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector3), "rbaz");
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &body_size));
  EXPECT_EQ(body_size, 4);

  // Clear up the current_request_body_ pointer.
  filter.current_request_body_ = nullptr;

  // Everything should return false again.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &chunks_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, &body_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedRequestBody, {nullptr, 0}));
}

TEST(ABIImpl, BufferedRequestBody) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  filter.setDecoderFilterCallbacks(callbacks);

  EXPECT_CALL(callbacks, decodingBuffer()).WillRepeatedly(testing::Return(nullptr));
  EXPECT_CALL(callbacks, modifyDecodingBuffer(_)).Times(testing::AnyNumber());
  EXPECT_CALL(callbacks, addDecodedData(_, _)).Times(testing::AnyNumber());

  size_t chunks_size = 0;
  size_t body_size = 0;

  // Non buffered buffer should return false.
  EXPECT_CALL(callbacks, decodingBuffer()).WillRepeatedly(testing::ReturnNull());
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &chunks_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &body_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, 0));

  // Append to buffered body always success.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, {nullptr, 0}));

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(callbacks, decodingBuffer()).WillRepeatedly(testing::Return(&buffer));
  EXPECT_CALL(callbacks, modifyDecodingBuffer(_))
      .WillRepeatedly(Invoke(
          [&](std::function<void(Buffer::Instance&)> callback) -> void { callback(buffer); }));
  EXPECT_CALL(callbacks, addDecodedData(_, true))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> void { buffer.add(data); }));

  // Empty buffer should return size 0 and drain should return work without problems.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &chunks_size));
  EXPECT_EQ(chunks_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &body_size));
  EXPECT_EQ(body_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, 0));

  // Append data to the buffer.
  const std::string data = "foo";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody,
      {data.data(), data.size()}));
  EXPECT_EQ(buffer.toString(), data);

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &chunks_size));
  auto result_buffer_vector = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody,
      result_buffer_vector.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector), data);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &body_size));
  EXPECT_EQ(body_size, 3);

  // Add more data to the buffer.
  const std::string data2 = "bar";
  const std::string data3 = "baz";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody,
      {data2.data(), data2.size()}));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody,
      {data3.data(), data3.size()}));
  EXPECT_EQ(buffer.toString(), data + data2 + data3);

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &chunks_size));
  auto result_buffer_vector2 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody,
      result_buffer_vector2.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector2), data + data2 + data3);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &body_size));
  EXPECT_EQ(body_size, 9);

  // Drain the first 5 bytes.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, 5));

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &chunks_size));
  auto result_buffer_vector3 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody,
      result_buffer_vector3.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector3), "rbaz");
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedRequestBody, &body_size));
  EXPECT_EQ(body_size, 4);
}

TEST(ABIImpl, ResponseBody) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  Http::MockStreamEncoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  filter.setEncoderFilterCallbacks(callbacks);

  size_t chunks_size = 0;
  size_t body_size = 0;

  // Non existing buffer should return false.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &chunks_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &body_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, {nullptr, 0}));

  Buffer::OwnedImpl buffer;
  filter.current_response_body_ = &buffer;

  // Empty buffer should return size 0 and drain should return work without problems.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &chunks_size));
  EXPECT_EQ(chunks_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &body_size));
  EXPECT_EQ(body_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, 0));

  // Append data to the buffer.
  const std::string data = "foo";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody,
      {data.data(), data.size()}));
  EXPECT_EQ(buffer.toString(), data);

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &chunks_size));
  auto result_buffer_vector = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody,
      result_buffer_vector.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector), data);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &body_size));
  EXPECT_EQ(body_size, 3);

  // Add more data to the buffer.
  const std::string data2 = "bar";
  const std::string data3 = "baz";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody,
      {data2.data(), data2.size()}));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody,
      {data3.data(), data3.size()}));
  EXPECT_EQ(buffer.toString(), data + data2 + data3);

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &chunks_size));
  auto result_buffer_vector2 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody,
      result_buffer_vector2.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector2), data + data2 + data3);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &body_size));
  EXPECT_EQ(body_size, 9);

  // Drain the first 5 bytes.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, 5));

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &chunks_size));
  auto result_buffer_vector3 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody,
      result_buffer_vector3.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector3), "rbaz");
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &body_size));
  EXPECT_EQ(body_size, 4);

  // Clear up the current_response_body_ pointer.
  filter.current_response_body_ = nullptr;

  // Everything should return false again.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &chunks_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, &body_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, 0));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_ReceivedResponseBody, {nullptr, 0}));
}

TEST(ABIImpl, BufferedResponseBody) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  Http::MockStreamEncoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  filter.setEncoderFilterCallbacks(callbacks);

  EXPECT_CALL(callbacks, encodingBuffer()).WillRepeatedly(testing::Return(nullptr));
  EXPECT_CALL(callbacks, modifyEncodingBuffer(_)).Times(testing::AnyNumber());
  EXPECT_CALL(callbacks, addEncodedData(_, _)).Times(testing::AnyNumber());

  size_t chunks_size = 0;
  size_t body_size = 0;

  // Non existing buffer should return false.
  EXPECT_CALL(callbacks, encodingBuffer()).WillRepeatedly(testing::ReturnNull());
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, nullptr));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &chunks_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &body_size));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, 0));

  // Append to buffered body always success.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, {nullptr, 0}));

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(callbacks, encodingBuffer()).WillRepeatedly(testing::Return(&buffer));
  EXPECT_CALL(callbacks, modifyEncodingBuffer(_))
      .WillRepeatedly(Invoke(
          [&](std::function<void(Buffer::Instance&)> callback) -> void { callback(buffer); }));
  EXPECT_CALL(callbacks, addEncodedData(_, true))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> void { buffer.add(data); }));

  // Empty buffer should return size 0 and drain should return work without problems.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &chunks_size));
  EXPECT_EQ(chunks_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &body_size));
  EXPECT_EQ(body_size, 0);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, 0));

  // Append data to the buffer.
  const std::string data = "foo";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody,
      {data.data(), data.size()}));
  EXPECT_EQ(buffer.toString(), data);

  // Get the data from the buffer.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &chunks_size));
  auto result_buffer_vector = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody,
      result_buffer_vector.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector), data);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &body_size));
  EXPECT_EQ(body_size, 3);

  // Add more data to the buffer.
  const std::string data2 = "bar";
  const std::string data3 = "baz";
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody,
      {data2.data(), data2.size()}));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_append_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody,
      {data3.data(), data3.size()}));
  EXPECT_EQ(buffer.toString(), data + data2 + data3);

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &chunks_size));
  auto result_buffer_vector2 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody,
      result_buffer_vector2.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector2), data + data2 + data3);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &body_size));
  EXPECT_EQ(body_size, 9);

  // Drain the first 5 bytes.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_drain_body(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, 5));

  // Check the data.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &chunks_size));
  auto result_buffer_vector3 = std::vector<envoy_dynamic_module_type_envoy_buffer>(chunks_size);
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_chunks(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody,
      result_buffer_vector3.data()));
  EXPECT_EQ(bufferVectorToString(result_buffer_vector3), "rbaz");
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_body_size(
      &filter, envoy_dynamic_module_type_http_body_type_BufferedResponseBody, &body_size));
  EXPECT_EQ(body_size, 4);
}

TEST(ABIImpl, ClearRouteCache) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  filter.setDecoderFilterCallbacks(callbacks);
  Http::MockDownstreamStreamFilterCallbacks downstream_callbacks;
  EXPECT_CALL(downstream_callbacks, clearRouteCache());
  EXPECT_CALL(callbacks, downstreamCallbacks())
      .WillOnce(testing::Return(OptRef(downstream_callbacks)));
  envoy_dynamic_module_callback_http_clear_route_cache(&filter);
}

TEST(ABIImpl, GetAttributes) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter_without_callbacks{nullptr, symbol_table};
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));
  filter.setDecoderFilterCallbacks(callbacks);
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(testing::Return(Http::Protocol::Http11));
  EXPECT_CALL(stream_info, upstreamInfo()).Times(testing::AtLeast(1));
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(testing::Return(200));
  StreamInfo::StreamIdProviderImpl id_provider("ffffffff-0012-0110-00ff-0c00400600ff");
  EXPECT_CALL(stream_info, getStreamIdProvider())
      .WillRepeatedly(testing::Return(makeOptRef<const StreamInfo::StreamIdProvider>(id_provider)));
  const std::string route_name{"test_route"};
  EXPECT_CALL(stream_info, getRouteName()).WillRepeatedly(testing::ReturnRef(route_name));

  NiceMock<StreamInfo::MockStreamInfo> info;
  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .WillRepeatedly(testing::ReturnPointee(info.downstream_connection_info_provider_));
  info.downstream_connection_info_provider_->setRemoteAddress(
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.1.1.1", 1234, false));
  info.downstream_connection_info_provider_->setLocalAddress(
      Envoy::Network::Utility::parseInternetAddressNoThrow("127.0.0.2", 4321, false));

  envoy_dynamic_module_type_envoy_buffer result_buffer = {nullptr, 0};
  uint64_t result_number = 0;

  // envoy_dynamic_module_type_attribute_id_RequestPath with null headers map, should return false.
  EXPECT_CALL(callbacks, requestHeaders()).WillOnce(testing::Return(absl::nullopt));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestPath, &result_buffer));

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {":path", "/api/v1/action?param=value"},
      {":scheme", "https"},
      {":method", "GET"},
      {":authority", "example.org"},
      {"referer", "envoyproxy.io"},
      {"user-agent", "curl/7.54.1"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  EXPECT_CALL(callbacks, requestHeaders())
      .WillRepeatedly(testing::Return(makeOptRef<RequestHeaderMap>(request_headers)));

  // Unsupported attributes.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter, envoy_dynamic_module_type_attribute_id_XdsListenerMetadata, &result_number));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_XdsListenerMetadata, &result_buffer));

  // Type mismatch.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter, envoy_dynamic_module_type_attribute_id_SourceAddress, &result_number));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_SourcePort, &result_buffer));

  // envoy_dynamic_module_type_attribute_id_RequestProtocol
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestProtocol, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "HTTP/1.1");

  // envoy_dynamic_module_type_attribute_id_UpstreamAddress
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_UpstreamAddress, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "10.0.0.1:443");

  // envoy_dynamic_module_type_attribute_id_SourceAddress
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_SourceAddress, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "1.1.1.1:1234");

  // envoy_dynamic_module_type_attribute_id_DestinationAddress
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_DestinationAddress, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "127.0.0.2:4321");

  // envoy_dynamic_module_type_attribute_id_RequestId
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestId, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length),
            "ffffffff-0012-0110-00ff-0c00400600ff");

  // envoy_dynamic_module_type_attribute_id_XdsRouteName
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_XdsRouteName, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "test_route");

  // envoy_dynamic_module_type_attribute_id_RequestPath
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestPath, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "/api/v1/action?param=value");

  // envoy_dynamic_module_type_attribute_id_RequestHost
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestHost, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "example.org");

  // envoy_dynamic_module_type_attribute_id_RequestMethod
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestMethod, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "GET");

  // envoy_dynamic_module_type_attribute_id_RequestScheme
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestScheme, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "https");

  // envoy_dynamic_module_type_attribute_id_RequestUserAgent
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestUserAgent, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "curl/7.54.1");

  // envoy_dynamic_module_type_attribute_id_RequestReferer
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestReferer, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "envoyproxy.io");

  // envoy_dynamic_module_type_attribute_id_RequestQuery
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestQuery, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "param=value");

  // envoy_dynamic_module_type_attribute_id_RequestUrlPath
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestUrlPath, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "/api/v1/action");

  // test again without query params for envoy_dynamic_module_type_attribute_id_RequestUrlPath
  request_headers.setPath("/api/v1/action");
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_RequestUrlPath, &result_buffer));
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), "/api/v1/action");

  // empty connection, should return false
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter_without_callbacks, envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion,
      &result_buffer));

  EXPECT_CALL(callbacks, connection()).WillRepeatedly(testing::Return(absl::nullopt));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionUriSanPeerCertificate,
      &result_buffer));

  // tests for TLS connection attributes
  const Network::MockConnection connection;
  EXPECT_CALL(callbacks, connection())
      .WillRepeatedly(
          testing::Return(makeOptRef(dynamic_cast<const Network::Connection&>(connection))));
  EXPECT_CALL(connection, ssl()).WillRepeatedly(testing::Return(nullptr));
  // no TLS, should return false
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion, &result_buffer));

  // mock TLS and its attributes
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(testing::Return(ssl));
  const std::string tls_version = "TLSv1.2";
  EXPECT_CALL(*ssl, tlsVersion()).WillRepeatedly(testing::ReturnRef(tls_version));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion, &result_buffer));
  EXPECT_EQ(result_buffer.length, tls_version.size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), tls_version);

  const std::string digest = "sha256_digest";
  EXPECT_CALL(*ssl, sha256PeerCertificateDigest()).WillRepeatedly(testing::ReturnRef(digest));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest,
      &result_buffer));
  EXPECT_EQ(result_buffer.length, digest.size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), digest);

  const std::string subject_cert = "subject_cert";
  EXPECT_CALL(*ssl, subjectLocalCertificate()).WillRepeatedly(testing::ReturnRef(subject_cert));
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(testing::ReturnRef(subject_cert));

  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate,
      &result_buffer));
  EXPECT_EQ(result_buffer.length, subject_cert.size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), subject_cert);

  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate,
      &result_buffer));
  EXPECT_EQ(result_buffer.length, subject_cert.size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), subject_cert);

  // test returning the first entry when there are multiple SANs
  const std::vector<std::string> sans_empty;
  EXPECT_CALL(*ssl, dnsSansLocalCertificate()).WillOnce(testing::Return(sans_empty));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionDnsSanLocalCertificate,
      &result_buffer));

  const std::vector<std::string> sans = {"alt_name1", "alt_name2"};
  EXPECT_CALL(*ssl, dnsSansLocalCertificate()).WillRepeatedly(testing::Return(sans));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(testing::Return(sans));
  EXPECT_CALL(*ssl, uriSanLocalCertificate()).WillRepeatedly(testing::Return(sans));
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(testing::Return(sans));

  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionDnsSanLocalCertificate,
      &result_buffer));
  EXPECT_EQ(result_buffer.length, sans[0].size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), sans[0]);

  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionDnsSanPeerCertificate,
      &result_buffer));
  EXPECT_EQ(result_buffer.length, sans[0].size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), sans[0]);

  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionUriSanLocalCertificate,
      &result_buffer));
  EXPECT_EQ(result_buffer.length, sans[0].size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), sans[0]);

  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_string(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionUriSanPeerCertificate,
      &result_buffer));
  EXPECT_EQ(result_buffer.length, sans[0].size());
  EXPECT_EQ(std::string(result_buffer.ptr, result_buffer.length), sans[0]);

  // envoy_dynamic_module_type_attribute_id_ResponseCode
  EXPECT_FALSE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter_without_callbacks, envoy_dynamic_module_type_attribute_id_ResponseCode,
      &result_number));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter, envoy_dynamic_module_type_attribute_id_ResponseCode, &result_number));
  EXPECT_EQ(result_number, 200);

  // envoy_dynamic_module_type_attribute_id_UpstreamPort
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter, envoy_dynamic_module_type_attribute_id_UpstreamPort, &result_number));
  EXPECT_EQ(result_number, 443);

  // envoy_dynamic_module_type_attribute_id_SourcePort
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter, envoy_dynamic_module_type_attribute_id_SourcePort, &result_number));
  EXPECT_EQ(result_number, 1234);

  // envoy_dynamic_module_type_attribute_id_DestinationPort
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter, envoy_dynamic_module_type_attribute_id_DestinationPort, &result_number));
  EXPECT_EQ(result_number, 4321);

  // envoy_dynamic_module_type_attribute_id_ConnectionId
  EXPECT_CALL(connection, id()).WillRepeatedly(testing::Return(8386));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_filter_get_attribute_int(
      &filter, envoy_dynamic_module_type_attribute_id_ConnectionId, &result_number));
  EXPECT_EQ(result_number, 8386);
}

TEST(ABIImpl, HttpCallout) {
  Stats::SymbolTableImpl symbol_table;
  DynamicModuleHttpFilter filter{nullptr, symbol_table};
  const std::string cluster{"some_cluster"};
  uint64_t callout_id = 0;
  EXPECT_EQ(envoy_dynamic_module_callback_http_filter_http_callout(&filter, &callout_id,
                                                                   {cluster.data(), cluster.size()},
                                                                   nullptr, 0, {nullptr, 0}, 1000),
            envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders);
}

TEST(ABIImpl, Log) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::err);
  EXPECT_FALSE(
      envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level_Trace));
  EXPECT_FALSE(
      envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level_Debug));
  EXPECT_FALSE(envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level_Info));
  EXPECT_FALSE(envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level_Warn));
  EXPECT_TRUE(envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level_Error));
  EXPECT_TRUE(
      envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level_Critical));

  // Use all log levels, mostly for coverage.
  const std::string msg = "test log message";
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Trace,
                                    {msg.data(), msg.size()});
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Debug,
                                    {msg.data(), msg.size()});
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info,
                                    {msg.data(), msg.size()});
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Warn,
                                    {msg.data(), msg.size()});
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Error,
                                    {msg.data(), msg.size()});
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Critical,
                                    {msg.data(), msg.size()});
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Off,
                                    {msg.data(), msg.size()});
}

TEST(ABIImpl, Stats) {
  Stats::TestUtil::TestStore stats_store;
  Stats::TestUtil::TestScope stats_scope{"", stats_store};
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto filter_config = std::make_shared<DynamicModuleHttpFilterConfig>(
      "some_name", "some_config", nullptr, stats_scope, context);
  DynamicModuleHttpFilter filter{filter_config, stats_scope.symbolTable()};

  const std::string counter_name{"some_counter"};
  size_t counter_id;
  auto result = envoy_dynamic_module_callback_http_filter_config_define_counter(
      filter_config.get(), {counter_name.data(), counter_name.size()}, &counter_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  Stats::CounterOptConstRef counter =
      stats_store.findCounterByString("dynamicmodulescustom.some_counter");
  EXPECT_TRUE(counter.has_value());
  EXPECT_EQ(counter->get().value(), 0);
  result = envoy_dynamic_module_callback_http_filter_increment_counter(&filter, counter_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter->get().value(), 10);
  result = envoy_dynamic_module_callback_http_filter_increment_counter(&filter, counter_id, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter->get().value(), 52);

  const std::string counter_vec_name{"some_counter_vec"};
  const std::string counter_vec_label_name{"some_label"};
  std::vector<envoy_dynamic_module_type_module_buffer> counter_vec_labels = {
      {const_cast<char*>(counter_vec_label_name.data()), counter_vec_label_name.size()},
  };
  size_t counter_vec_id;
  result = envoy_dynamic_module_callback_http_filter_config_define_counter_vec(
      filter_config.get(), {counter_vec_name.data(), counter_vec_name.size()},
      counter_vec_labels.data(), counter_vec_labels.size(), &counter_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  const std::string counter_vec_label_value{"some_value"};
  std::vector<envoy_dynamic_module_type_module_buffer> counter_vec_labels_values = {
      {const_cast<char*>(counter_vec_label_value.data()), counter_vec_label_value.size()},
  };
  result = envoy_dynamic_module_callback_http_filter_increment_counter_vec(
      &filter, counter_vec_id, counter_vec_labels_values.data(), counter_vec_labels_values.size(),
      10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  Stats::CounterOptConstRef counter_vec = stats_store.findCounterByString(
      "dynamicmodulescustom.some_counter_vec.some_label.some_value");
  EXPECT_TRUE(counter_vec.has_value());
  EXPECT_EQ(counter_vec->get().value(), 10);
  result = envoy_dynamic_module_callback_http_filter_increment_counter_vec(
      &filter, counter_vec_id, counter_vec_labels_values.data(), counter_vec_labels_values.size(),
      10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_vec->get().value(), 20);
  result = envoy_dynamic_module_callback_http_filter_increment_counter_vec(
      &filter, counter_vec_id, counter_vec_labels_values.data(), counter_vec_labels_values.size(),
      42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_vec->get().value(), 62);

  const std::string gauge_name{"some_gauge"};
  size_t gauge_id;
  result = envoy_dynamic_module_callback_http_filter_config_define_gauge(
      filter_config.get(), {gauge_name.data(), gauge_name.size()}, &gauge_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  Stats::GaugeOptConstRef gauge = stats_store.findGaugeByString("dynamicmodulescustom.some_gauge");
  EXPECT_TRUE(gauge.has_value());
  EXPECT_EQ(gauge->get().value(), 0);
  result = envoy_dynamic_module_callback_http_filter_increment_gauge(&filter, gauge_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge->get().value(), 10);
  result = envoy_dynamic_module_callback_http_filter_increment_gauge(&filter, gauge_id, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge->get().value(), 52);
  result = envoy_dynamic_module_callback_http_filter_decrement_gauge(&filter, gauge_id, 50);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge->get().value(), 2);
  result = envoy_dynamic_module_callback_http_filter_set_gauge(&filter, gauge_id, 9001);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge->get().value(), 9001);

  const std::string gauge_vec_name{"some_gauge_vec"};
  const std::string gauge_vec_label_name{"some_label"};
  std::vector<envoy_dynamic_module_type_module_buffer> gauge_vec_labels = {
      {const_cast<char*>(gauge_vec_label_name.data()), gauge_vec_label_name.size()},
  };
  size_t gauge_vec_id;
  result = envoy_dynamic_module_callback_http_filter_config_define_gauge_vec(
      filter_config.get(), {gauge_vec_name.data(), gauge_vec_name.size()}, gauge_vec_labels.data(),
      gauge_vec_labels.size(), &gauge_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  const std::string gauge_vec_label_value{"some_value"};
  std::vector<envoy_dynamic_module_type_module_buffer> gauge_vec_labels_values = {
      {const_cast<char*>(gauge_vec_label_value.data()), gauge_vec_label_value.size()},
  };
  result = envoy_dynamic_module_callback_http_filter_increment_gauge_vec(
      &filter, gauge_vec_id, gauge_vec_labels_values.data(), gauge_vec_labels_values.size(), 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  Stats::GaugeOptConstRef gauge_vec =
      stats_store.findGaugeByString("dynamicmodulescustom.some_gauge_vec.some_label.some_value");
  EXPECT_TRUE(gauge_vec.has_value());
  EXPECT_EQ(gauge_vec->get().value(), 10);
  result = envoy_dynamic_module_callback_http_filter_increment_gauge_vec(
      &filter, gauge_vec_id, gauge_vec_labels_values.data(), gauge_vec_labels_values.size(), 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_vec->get().value(), 20);
  result = envoy_dynamic_module_callback_http_filter_decrement_gauge_vec(
      &filter, gauge_vec_id, gauge_vec_labels_values.data(), gauge_vec_labels_values.size(), 12);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_vec->get().value(), 8);
  result = envoy_dynamic_module_callback_http_filter_set_gauge_vec(
      &filter, gauge_vec_id, gauge_vec_labels_values.data(), gauge_vec_labels_values.size(), 9001);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_vec->get().value(), 9001);

  const std::string histogram_name{"some_histogram"};
  size_t histogram_id;
  result = envoy_dynamic_module_callback_http_filter_config_define_histogram(
      filter_config.get(), {histogram_name.data(), histogram_name.size()}, &histogram_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  Stats::HistogramOptConstRef histogram =
      stats_store.findHistogramByString("dynamicmodulescustom.some_histogram");
  EXPECT_TRUE(histogram.has_value());
  EXPECT_FALSE(stats_store.histogramRecordedValues("dynamicmodulescustom.some_histogram"));
  result =
      envoy_dynamic_module_callback_http_filter_record_histogram_value(&filter, histogram_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(stats_store.histogramValues("dynamicmodulescustom.some_histogram", false),
            (std::vector<uint64_t>{10}));
  result =
      envoy_dynamic_module_callback_http_filter_record_histogram_value(&filter, histogram_id, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(stats_store.histogramValues("dynamicmodulescustom.some_histogram", false),
            (std::vector<uint64_t>{10, 42}));

  const std::string histogram_vec_name{"some_histogram_vec"};
  const std::string histogram_vec_label_name{"some_label"};
  std::vector<envoy_dynamic_module_type_module_buffer> histogram_vec_labels = {
      {const_cast<char*>(histogram_vec_label_name.data()), histogram_vec_label_name.size()},
  };
  size_t histogram_vec_id;
  result = envoy_dynamic_module_callback_http_filter_config_define_histogram_vec(
      filter_config.get(), {histogram_vec_name.data(), histogram_vec_name.size()},
      histogram_vec_labels.data(), histogram_vec_labels.size(), &histogram_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  const std::string histogram_vec_label_value{"some_value"};
  std::vector<envoy_dynamic_module_type_module_buffer> histogram_vec_labels_values = {
      {const_cast<char*>(histogram_vec_label_value.data()), histogram_vec_label_value.size()},
  };
  result = envoy_dynamic_module_callback_http_filter_record_histogram_value_vec(
      &filter, histogram_vec_id, histogram_vec_labels_values.data(),
      histogram_vec_labels_values.size(), 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  Stats::HistogramOptConstRef histogram_vec = stats_store.findHistogramByString(
      "dynamicmodulescustom.some_histogram_vec.some_label.some_value");
  EXPECT_TRUE(histogram_vec.has_value());
  EXPECT_EQ(stats_store.histogramValues(
                "dynamicmodulescustom.some_histogram_vec.some_label.some_value", false),
            (std::vector<uint64_t>{10}));
  result = envoy_dynamic_module_callback_http_filter_record_histogram_value_vec(
      &filter, histogram_vec_id, histogram_vec_labels_values.data(),
      histogram_vec_labels_values.size(), 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(stats_store.histogramValues(
                "dynamicmodulescustom.some_histogram_vec.some_label.some_value", false),
            (std::vector<uint64_t>{10, 42}));

  // test using invalid stat id
  size_t invalid_stat_id = 9999;
  result =
      envoy_dynamic_module_callback_http_filter_increment_counter(&filter, invalid_stat_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_increment_counter_vec(
      &filter, invalid_stat_id, counter_vec_labels_values.data(), counter_vec_labels_values.size(),
      10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_increment_gauge(&filter, invalid_stat_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_decrement_gauge(&filter, invalid_stat_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_set_gauge(&filter, invalid_stat_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_increment_gauge_vec(
      &filter, invalid_stat_id, gauge_vec_labels_values.data(), gauge_vec_labels_values.size(), 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_decrement_gauge_vec(
      &filter, invalid_stat_id, gauge_vec_labels_values.data(), gauge_vec_labels_values.size(), 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_set_gauge_vec(
      &filter, invalid_stat_id, gauge_vec_labels_values.data(), gauge_vec_labels_values.size(), 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_record_histogram_value(&filter,
                                                                            invalid_stat_id, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
  result = envoy_dynamic_module_callback_http_filter_record_histogram_value_vec(
      &filter, invalid_stat_id, histogram_vec_labels_values.data(),
      histogram_vec_labels_values.size(), 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);

  // test using invalid labels
  result = envoy_dynamic_module_callback_http_filter_increment_counter_vec(&filter, counter_vec_id,
                                                                           {}, 0, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_InvalidLabels);
  result = envoy_dynamic_module_callback_http_filter_increment_gauge_vec(&filter, gauge_vec_id, {},
                                                                         0, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_InvalidLabels);
  result = envoy_dynamic_module_callback_http_filter_record_histogram_value_vec(
      &filter, histogram_vec_id, {}, 0, 10);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_InvalidLabels);

  // test stat creation after freezing
  filter_config->stat_creation_frozen_ = true;
  result = envoy_dynamic_module_callback_http_filter_config_define_counter(
      filter_config.get(), {counter_name.data(), counter_name.size()}, &counter_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Frozen);
  result = envoy_dynamic_module_callback_http_filter_config_define_counter_vec(
      filter_config.get(), {counter_vec_name.data(), counter_vec_name.size()},
      counter_vec_labels.data(), counter_vec_labels.size(), &counter_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Frozen);
  result = envoy_dynamic_module_callback_http_filter_config_define_gauge(
      filter_config.get(), {gauge_name.data(), gauge_name.size()}, &gauge_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Frozen);
  result = envoy_dynamic_module_callback_http_filter_config_define_gauge_vec(
      filter_config.get(), {gauge_vec_name.data(), gauge_vec_name.size()}, gauge_vec_labels.data(),
      gauge_vec_labels.size(), &gauge_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Frozen);
  result = envoy_dynamic_module_callback_http_filter_config_define_histogram(
      filter_config.get(), {histogram_name.data(), histogram_name.size()}, &histogram_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Frozen);
  result = envoy_dynamic_module_callback_http_filter_config_define_histogram_vec(
      filter_config.get(), {histogram_vec_name.data(), histogram_vec_name.size()},
      histogram_vec_labels.data(), histogram_vec_labels.size(), &histogram_vec_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Frozen);
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
