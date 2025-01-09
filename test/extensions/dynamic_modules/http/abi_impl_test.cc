#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

class DynamicModuleHttpFilterTest : public testing::Test {
public:
  void SetUp() override {
    filter_ = std::make_unique<DynamicModuleHttpFilter>(nullptr);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  std::unique_ptr<DynamicModuleHttpFilter> filter_;
};

// Parameterized test for get_header_value
using GetHeaderValueCallbackType = size_t (*)(envoy_dynamic_module_type_http_filter_envoy_ptr,
                                              envoy_dynamic_module_type_buffer_module_ptr, size_t,
                                              envoy_dynamic_module_type_buffer_envoy_ptr*, size_t*,
                                              size_t);

class DynamicModuleHttpFilterGetHeaderValueTest
    : public DynamicModuleHttpFilterTest,
      public ::testing::WithParamInterface<GetHeaderValueCallbackType> {};

TEST_P(DynamicModuleHttpFilterGetHeaderValueTest, GetHeaderValue) {
  GetHeaderValueCallbackType callback = GetParam();

  // Test with nullptr accessors.
  envoy_dynamic_module_type_buffer_envoy_ptr result_buffer_ptr;
  size_t result_buffer_length_ptr;
  size_t index = 0;
  const size_t res =
      callback(filter_.get(), nullptr, 0, &result_buffer_ptr, &result_buffer_length_ptr, index);
  EXPECT_EQ(res, 0);
  EXPECT_EQ(result_buffer_ptr, nullptr);
  EXPECT_EQ(result_buffer_length_ptr, 0);

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter_->request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter_->request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter_->response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter_->response_trailers_ = &response_trailers;

  // The key is not found.
  std::string key = "nonexistent";
  envoy_dynamic_module_type_buffer_module_ptr key_ptr = key.data();
  size_t key_length = key.size();
  result_buffer_ptr = nullptr;
  result_buffer_length_ptr = 0;
  index = 0;
  const size_t rc = callback(filter_.get(), key_ptr, key_length, &result_buffer_ptr,
                             &result_buffer_length_ptr, index);
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(result_buffer_ptr, nullptr);
  EXPECT_EQ(result_buffer_length_ptr, 0);

  // The key is found for single value.
  key = "single";
  key_ptr = key.data();
  key_length = key.size();

  result_buffer_ptr = nullptr;
  result_buffer_length_ptr = 0;
  index = 0;
  const size_t rc2 = callback(filter_.get(), key_ptr, key_length, &result_buffer_ptr,
                              &result_buffer_length_ptr, index);
  EXPECT_EQ(rc2, 1);
  EXPECT_NE(result_buffer_ptr, nullptr);
  EXPECT_EQ(result_buffer_length_ptr, 5);
  EXPECT_EQ(std::string(result_buffer_ptr, result_buffer_length_ptr), "value");

  // key is found for the single value but index is out of range.
  result_buffer_ptr = nullptr;
  result_buffer_length_ptr = 0;
  index = 1;
  const size_t rc3 = callback(filter_.get(), key_ptr, key_length, &result_buffer_ptr,
                              &result_buffer_length_ptr, index);
  EXPECT_EQ(rc3, 1);
  EXPECT_EQ(result_buffer_ptr, nullptr);
  EXPECT_EQ(result_buffer_length_ptr, 0);

  // The key is found for multiple values.
  key = "multi";
  key_ptr = key.data();
  key_length = key.size();

  result_buffer_ptr = nullptr;
  result_buffer_length_ptr = 0;
  index = 0;
  const size_t rc4 = callback(filter_.get(), key_ptr, key_length, &result_buffer_ptr,
                              &result_buffer_length_ptr, index);
  EXPECT_EQ(rc4, 2);
  EXPECT_NE(result_buffer_ptr, nullptr);
  EXPECT_EQ(result_buffer_length_ptr, 6);
  EXPECT_EQ(std::string(result_buffer_ptr, result_buffer_length_ptr), "value1");

  result_buffer_ptr = nullptr;
  result_buffer_length_ptr = 0;
  index = 1;
  const size_t rc5 = callback(filter_.get(), key_ptr, key_length, &result_buffer_ptr,
                              &result_buffer_length_ptr, index);
  EXPECT_EQ(rc5, 2);
  EXPECT_NE(result_buffer_ptr, nullptr);
  EXPECT_EQ(result_buffer_length_ptr, 6);
  EXPECT_EQ(std::string(result_buffer_ptr, result_buffer_length_ptr), "value2");

  // The key is found for multiple values but index is out of range.
  result_buffer_ptr = nullptr;
  result_buffer_length_ptr = 0;
  index = 2;
  const size_t rc6 = callback(filter_.get(), key_ptr, key_length, &result_buffer_ptr,
                              &result_buffer_length_ptr, index);
  EXPECT_EQ(rc6, 2);
  EXPECT_EQ(result_buffer_ptr, nullptr);
  EXPECT_EQ(result_buffer_length_ptr, 0);
}

INSTANTIATE_TEST_SUITE_P(
    GetHeaderValueTests, DynamicModuleHttpFilterGetHeaderValueTest,
    ::testing::Values(envoy_dynamic_module_callback_http_get_request_header,
                      envoy_dynamic_module_callback_http_get_request_trailer,
                      envoy_dynamic_module_callback_http_get_response_header,
                      envoy_dynamic_module_callback_http_get_response_trailer));

// Parameterized test for set_header_value
using SetHeaderValueCallbackType = bool (*)(envoy_dynamic_module_type_http_filter_envoy_ptr,
                                            envoy_dynamic_module_type_buffer_module_ptr, size_t,
                                            envoy_dynamic_module_type_buffer_module_ptr, size_t);

class DynamicModuleHttpFilterSetHeaderValueTest
    : public DynamicModuleHttpFilterTest,
      public ::testing::WithParamInterface<SetHeaderValueCallbackType> {};

TEST_P(DynamicModuleHttpFilterSetHeaderValueTest, SetHeaderValue) {
  SetHeaderValueCallbackType callback = GetParam();

  // Test with nullptr accessors.
  const std::string key = "key";
  const std::string value = "value";
  envoy_dynamic_module_type_buffer_envoy_ptr key_ptr = const_cast<char*>(key.data());
  size_t key_length = key.size();
  envoy_dynamic_module_type_buffer_envoy_ptr value_ptr = const_cast<char*>(value.data());
  size_t value_length = value.size();
  EXPECT_FALSE(callback(filter_.get(), key_ptr, key_length, value_ptr, value_length));

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter_->request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter_->request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter_->response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter_->response_trailers_ = &response_trailers;

  Http::HeaderMap* header_map = nullptr;
  if (callback == &envoy_dynamic_module_callback_http_set_request_header) {
    header_map = &request_headers;
  } else if (callback == &envoy_dynamic_module_callback_http_set_request_trailer) {
    header_map = &request_trailers;
  } else if (callback == &envoy_dynamic_module_callback_http_set_response_header) {
    header_map = &response_headers;
  } else if (callback == &envoy_dynamic_module_callback_http_set_response_trailer) {
    header_map = &response_trailers;
  } else {
    FAIL();
  }

  // Non existing key.
  const std::string new_key = "new_one";
  const std::string new_value = "value";
  envoy_dynamic_module_type_buffer_envoy_ptr new_key_ptr = const_cast<char*>(new_key.data());
  size_t new_key_length = new_key.size();
  envoy_dynamic_module_type_buffer_envoy_ptr new_value_ptr = const_cast<char*>(new_value.data());
  size_t new_value_length = new_value.size();
  EXPECT_TRUE(
      callback(filter_.get(), new_key_ptr, new_key_length, new_value_ptr, new_value_length));

  auto values = header_map->get(Envoy::Http::LowerCaseString(new_key));
  EXPECT_EQ(values.size(), 1);
  EXPECT_EQ(values[0]->value().getStringView(), new_value);

  // Existing non-multi key.
  const std::string key2 = "single";
  const std::string value2 = "new_value";
  envoy_dynamic_module_type_buffer_envoy_ptr key_ptr2 = const_cast<char*>(key2.data());
  size_t key_length2 = key2.size();
  envoy_dynamic_module_type_buffer_envoy_ptr value_ptr2 = const_cast<char*>(value2.data());
  size_t value_length2 = value2.size();
  EXPECT_TRUE(callback(filter_.get(), key_ptr2, key_length2, value_ptr2, value_length2));

  auto values2 = header_map->get(Envoy::Http::LowerCaseString(key2));
  EXPECT_EQ(values2.size(), 1);
  EXPECT_EQ(values2[0]->value().getStringView(), value2);

  // Existing multi key must be replaced by a single value.
  const std::string key3 = "multi";
  const std::string value3 = "new_value";
  envoy_dynamic_module_type_buffer_envoy_ptr key_ptr3 = const_cast<char*>(key3.data());
  size_t key_length3 = key3.size();
  envoy_dynamic_module_type_buffer_envoy_ptr value_ptr3 = const_cast<char*>(value3.data());
  size_t value_length3 = value3.size();
  EXPECT_TRUE(callback(filter_.get(), key_ptr3, key_length3, value_ptr3, value_length3));

  auto values3 = header_map->get(Envoy::Http::LowerCaseString(key3));
  EXPECT_EQ(values3.size(), 1);
  EXPECT_EQ(values3[0]->value().getStringView(), value3);
}

INSTANTIATE_TEST_SUITE_P(
    SetHeaderValueTests, DynamicModuleHttpFilterSetHeaderValueTest,
    ::testing::Values(envoy_dynamic_module_callback_http_set_request_header,
                      envoy_dynamic_module_callback_http_set_request_trailer,
                      envoy_dynamic_module_callback_http_set_response_header,
                      envoy_dynamic_module_callback_http_set_response_trailer));

// Parameterized test for get_headers_count
using GetHeadersCountCallbackType = size_t (*)(envoy_dynamic_module_type_http_filter_envoy_ptr);

class DynamicModuleHttpFilterGetHeadersCountTest
    : public DynamicModuleHttpFilterTest,
      public ::testing::WithParamInterface<GetHeadersCountCallbackType> {};

TEST_P(DynamicModuleHttpFilterGetHeadersCountTest, GetHeadersCount) {
  GetHeadersCountCallbackType callback = GetParam();

  // Test with nullptr accessors.
  EXPECT_EQ(callback(filter_.get()), 0);

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter_->request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter_->request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter_->response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter_->response_trailers_ = &response_trailers;

  EXPECT_EQ(callback(filter_.get()), 3);
}

INSTANTIATE_TEST_SUITE_P(
    GetHeadersCountTests, DynamicModuleHttpFilterGetHeadersCountTest,
    ::testing::Values(envoy_dynamic_module_callback_http_get_request_headers_count,
                      envoy_dynamic_module_callback_http_get_request_trailers_count,
                      envoy_dynamic_module_callback_http_get_response_headers_count,
                      envoy_dynamic_module_callback_http_get_response_trailers_count));

// Parameterized test for get_headers
using GetHeadersCallbackType = bool (*)(envoy_dynamic_module_type_http_filter_envoy_ptr,
                                        envoy_dynamic_module_type_http_header*);

class DynamicModuleHttpFilterGetHeadersTest
    : public DynamicModuleHttpFilterTest,
      public ::testing::WithParamInterface<GetHeadersCallbackType> {};

TEST_P(DynamicModuleHttpFilterGetHeadersTest, GetHeaders) {
  GetHeadersCallbackType callback = GetParam();

  // Test with nullptr accessors.
  envoy_dynamic_module_type_http_header result_headers[3];
  EXPECT_FALSE(callback(filter_.get(), result_headers));

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter_->request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter_->request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter_->response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter_->response_trailers_ = &response_trailers;

  EXPECT_TRUE(callback(filter_.get(), result_headers));

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

INSTANTIATE_TEST_SUITE_P(
    GetHeadersTests, DynamicModuleHttpFilterGetHeadersTest,
    ::testing::Values(envoy_dynamic_module_callback_http_get_request_headers,
                      envoy_dynamic_module_callback_http_get_request_trailers,
                      envoy_dynamic_module_callback_http_get_response_headers,
                      envoy_dynamic_module_callback_http_get_response_trailers));

TEST_F(DynamicModuleHttpFilterTest, SendResponseNullptr) {
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq(""), _,
                                                 testing::Eq(0), testing::Eq("dynamic_module")));
  envoy_dynamic_module_callback_http_send_response(filter_.get(), 200, nullptr, 3, nullptr, 0);
}

TEST_F(DynamicModuleHttpFilterTest, SendResponseEmptyResponse) {
  Http::TestResponseHeaderMapImpl response_headers;
  filter_->response_headers_ = &response_headers;

  // Test with empty response.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq(""), _,
                                                 testing::Eq(0), testing::Eq("dynamic_module")));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));

  envoy_dynamic_module_callback_http_send_response(filter_.get(), 200, nullptr, 3, nullptr, 0);
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
                                                   header_count, nullptr, 0);
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
  envoy_dynamic_module_type_buffer_module_ptr body = const_cast<char*>(body_str.data());
  size_t body_length = body_str.size();
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::OK, testing::Eq("body"), _,
                                                 testing::Eq(0), testing::Eq("dynamic_module")));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _)).WillOnce(Invoke([](auto& headers, auto) {
    EXPECT_EQ(headers.get(Http::LowerCaseString("single"))[0]->value().getStringView(), "value");
    EXPECT_EQ(headers.get(Http::LowerCaseString("multi"))[0]->value().getStringView(), "value1");
    EXPECT_EQ(headers.get(Http::LowerCaseString("multi"))[1]->value().getStringView(), "value2");
  }));
  envoy_dynamic_module_callback_http_send_response(filter_.get(), 200, header_array.get(), 3, body,
                                                   body_length);
}

TEST(ABIImpl, dynamic_metadata) {
  DynamicModuleHttpFilter filter{nullptr};
  const std::string namespace_str = "foo";
  const std::string key_str = "key";
  envoy_dynamic_module_type_buffer_module_ptr namespace_ptr =
      const_cast<char*>(namespace_str.data());
  size_t namespace_length = namespace_str.size();
  envoy_dynamic_module_type_buffer_module_ptr key_ptr = const_cast<char*>(key_str.data());
  size_t key_length = key_str.size();
  double value = 42;
  const std::string value_str = "value";
  envoy_dynamic_module_type_buffer_module_ptr value_ptr = const_cast<char*>(value_str.data());
  size_t value_length = value_str.size();
  double result_number = 0;
  char* result_str_ptr = nullptr;
  size_t result_str_length = 0;

  // No stream info.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, value));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, value_ptr, value_length));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_dynamic_metadata_number(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_number));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_dynamic_metadata_string(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_str_ptr,
      &result_str_length));

  // No namespace.
  Http::MockStreamDecoderFilterCallbacks callbacks;
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(stream_info));
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));
  filter.setDecoderFilterCallbacks(callbacks);
  // Only tests get methods as setters create the namespace.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_dynamic_metadata_number(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_number));
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_dynamic_metadata_string(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_str_ptr,
      &result_str_length));

  // With namespace and key.
  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, value));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_dynamic_metadata_number(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_number));
  EXPECT_EQ(result_number, value);
  // Wrong type.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_dynamic_metadata_string(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_str_ptr,
      &result_str_length));

  EXPECT_TRUE(envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, value_ptr, value_length));
  EXPECT_TRUE(envoy_dynamic_module_callback_http_get_dynamic_metadata_string(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_str_ptr,
      &result_str_length));
  EXPECT_EQ(result_str_length, value_length);
  EXPECT_EQ(std::string(result_str_ptr, result_str_length), value_str);
  // Wrong type.
  EXPECT_FALSE(envoy_dynamic_module_callback_http_get_dynamic_metadata_number(
      &filter, namespace_ptr, namespace_length, key_ptr, key_length, &result_number));
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
