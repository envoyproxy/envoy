#include "source/extensions/filters/http/dynamic_modules/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

TEST(ABIImpl, get_header_value) {
  std::vector<size_t (*)(envoy_dynamic_module_type_http_filter_envoy_ptr,
                         envoy_dynamic_module_type_buffer_module_ptr, size_t,
                         envoy_dynamic_module_type_buffer_envoy_ptr*, size_t*, size_t)>
      callbacks = {envoy_dynamic_module_callback_http_get_request_header,
                   envoy_dynamic_module_callback_http_get_request_trailer,
                   envoy_dynamic_module_callback_http_get_response_header,
                   envoy_dynamic_module_callback_http_get_response_trailer};

  DynamicModuleHttpFilter filter{nullptr};

  // Test with nullptr accessors.
  envoy_dynamic_module_type_buffer_envoy_ptr result_buffer_ptr;
  size_t result_buffer_length_ptr;
  size_t index = 0;
  for (auto callback : callbacks) {
    const size_t res =
        callback(&filter, nullptr, 0, &result_buffer_ptr, &result_buffer_length_ptr, index);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(result_buffer_ptr, nullptr);
    EXPECT_EQ(result_buffer_length_ptr, 0);
  }

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter.request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter.request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter.response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter.response_trailers_ = &response_trailers;

  for (auto callback : callbacks) {
    // The key is not found.
    std::string key = "nonexistent";
    envoy_dynamic_module_type_buffer_module_ptr key_ptr = key.data();
    size_t key_length = key.size();
    result_buffer_ptr = nullptr;
    result_buffer_length_ptr = 0;
    index = 0;
    const size_t rc = callback(&filter, key_ptr, key_length, &result_buffer_ptr,
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
    const size_t rc2 = callback(&filter, key_ptr, key_length, &result_buffer_ptr,
                                &result_buffer_length_ptr, index);
    EXPECT_EQ(rc2, 1);
    EXPECT_NE(result_buffer_ptr, nullptr);
    EXPECT_EQ(result_buffer_length_ptr, 5);
    EXPECT_EQ(std::string(result_buffer_ptr, result_buffer_length_ptr), "value");

    // key is found for the single value but index is out of range.
    result_buffer_ptr = nullptr;
    result_buffer_length_ptr = 0;
    index = 1;
    const size_t rc3 = callback(&filter, key_ptr, key_length, &result_buffer_ptr,
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
    const size_t rc4 = callback(&filter, key_ptr, key_length, &result_buffer_ptr,
                                &result_buffer_length_ptr, index);
    EXPECT_EQ(rc4, 2);
    EXPECT_NE(result_buffer_ptr, nullptr);
    EXPECT_EQ(result_buffer_length_ptr, 6);
    EXPECT_EQ(std::string(result_buffer_ptr, result_buffer_length_ptr), "value1");

    result_buffer_ptr = nullptr;
    result_buffer_length_ptr = 0;
    index = 1;
    const size_t rc5 = callback(&filter, key_ptr, key_length, &result_buffer_ptr,
                                &result_buffer_length_ptr, index);
    EXPECT_EQ(rc5, 2);
    EXPECT_NE(result_buffer_ptr, nullptr);
    EXPECT_EQ(result_buffer_length_ptr, 6);
    EXPECT_EQ(std::string(result_buffer_ptr, result_buffer_length_ptr), "value2");

    // The key is found for multiple values but index is out of range.
    result_buffer_ptr = nullptr;
    result_buffer_length_ptr = 0;
    index = 2;
    const size_t rc6 = callback(&filter, key_ptr, key_length, &result_buffer_ptr,
                                &result_buffer_length_ptr, index);
    EXPECT_EQ(rc6, 2);
    EXPECT_EQ(result_buffer_ptr, nullptr);
    EXPECT_EQ(result_buffer_length_ptr, 0);
  }
}

TEST(ABIImpl, set_header_value) {
  std::vector<bool (*)(envoy_dynamic_module_type_http_filter_envoy_ptr,
                       envoy_dynamic_module_type_buffer_module_ptr, size_t,
                       envoy_dynamic_module_type_buffer_module_ptr, size_t)>
      callbacks = {envoy_dynamic_module_callback_http_set_request_header,
                   envoy_dynamic_module_callback_http_set_request_trailer,
                   envoy_dynamic_module_callback_http_set_response_header,
                   envoy_dynamic_module_callback_http_set_response_trailer};

  DynamicModuleHttpFilter filter{nullptr};

  // Test with nullptr accessors.
  for (auto callback : callbacks) {
    const std::string key = "key";
    const std::string value = "value";
    envoy_dynamic_module_type_buffer_envoy_ptr key_ptr = const_cast<char*>(key.data());
    size_t key_length = key.size();
    envoy_dynamic_module_type_buffer_envoy_ptr value_ptr = const_cast<char*>(value.data());
    size_t value_length = value.size();
    EXPECT_FALSE(callback(&filter, key_ptr, key_length, value_ptr, value_length));
  }

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter.request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter.request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter.response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter.response_trailers_ = &response_trailers;

  for (auto callback : callbacks) {
    // Non existing key.
    const std::string key = "new_one";
    const std::string value = "value";
    envoy_dynamic_module_type_buffer_envoy_ptr key_ptr = const_cast<char*>(key.data());
    size_t key_length = key.size();
    envoy_dynamic_module_type_buffer_envoy_ptr value_ptr = const_cast<char*>(value.data());
    size_t value_length = value.size();
    EXPECT_TRUE(callback(&filter, key_ptr, key_length, value_ptr, value_length));

    auto values = request_headers.get(Envoy::Http::LowerCaseString(key));
    EXPECT_EQ(values.size(), 1);
    EXPECT_EQ(values[0]->value().getStringView(), value);

    // Existing non-multi key.
    const std::string key2 = "single";
    const std::string value2 = "new_value";
    envoy_dynamic_module_type_buffer_envoy_ptr key_ptr2 = const_cast<char*>(key2.data());
    size_t key_length2 = key2.size();
    envoy_dynamic_module_type_buffer_envoy_ptr value_ptr2 = const_cast<char*>(value2.data());
    size_t value_length2 = value2.size();
    EXPECT_TRUE(callback(&filter, key_ptr2, key_length2, value_ptr2, value_length2));

    auto values2 = request_headers.get(Envoy::Http::LowerCaseString(key2));
    EXPECT_EQ(values2.size(), 1);
    EXPECT_EQ(values2[0]->value().getStringView(), value2);

    // Existing multi key must be replaced by a single value.
    const std::string key3 = "multi";
    const std::string value3 = "new_value";
    envoy_dynamic_module_type_buffer_envoy_ptr key_ptr3 = const_cast<char*>(key3.data());
    size_t key_length3 = key3.size();
    envoy_dynamic_module_type_buffer_envoy_ptr value_ptr3 = const_cast<char*>(value3.data());
    size_t value_length3 = value3.size();
    EXPECT_TRUE(callback(&filter, key_ptr3, key_length3, value_ptr3, value_length3));

    auto values3 = request_headers.get(Envoy::Http::LowerCaseString(key3));
    EXPECT_EQ(values3.size(), 1);
    EXPECT_EQ(values3[0]->value().getStringView(), value3);
  }
}

TEST(ABIImpl, get_headers_count) {
  std::vector<size_t (*)(envoy_dynamic_module_type_http_filter_envoy_ptr)> callbacks = {
      envoy_dynamic_module_callback_http_get_request_headers_count,
      envoy_dynamic_module_callback_http_get_request_trailers_count,
      envoy_dynamic_module_callback_http_get_response_headers_count,
      envoy_dynamic_module_callback_http_get_response_trailers_count};

  DynamicModuleHttpFilter filter{nullptr};

  // Test with nullptr accessors.
  for (auto callback : callbacks) {
    EXPECT_EQ(callback(&filter), 0);
  }

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter.request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter.request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter.response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter.response_trailers_ = &response_trailers;

  for (auto callback : callbacks) {
    EXPECT_EQ(callback(&filter), 3);
  }
}

TEST(ABIImpl, get_headers) {
  std::vector<bool (*)(envoy_dynamic_module_type_http_filter_envoy_ptr,
                       envoy_dynamic_module_type_http_header*)>
      callbacks = {envoy_dynamic_module_callback_http_get_request_headers,
                   envoy_dynamic_module_callback_http_get_request_trailers,
                   envoy_dynamic_module_callback_http_get_response_headers,
                   envoy_dynamic_module_callback_http_get_response_trailers};

  DynamicModuleHttpFilter filter{nullptr};

  // Test with nullptr accessors.
  for (auto callback : callbacks) {
    envoy_dynamic_module_type_http_header result_headers[3];
    EXPECT_FALSE(callback(&filter, result_headers));
  }

  std::initializer_list<std::pair<std::string, std::string>> headers = {
      {"single", "value"}, {"multi", "value1"}, {"multi", "value2"}};
  Http::TestRequestHeaderMapImpl request_headers{headers};
  filter.request_headers_ = &request_headers;
  Http::TestRequestTrailerMapImpl request_trailers{headers};
  filter.request_trailers_ = &request_trailers;
  Http::TestResponseHeaderMapImpl response_headers{headers};
  filter.response_headers_ = &response_headers;
  Http::TestResponseTrailerMapImpl response_trailers{headers};
  filter.response_trailers_ = &response_trailers;

  for (auto callback : callbacks) {
    envoy_dynamic_module_type_http_header result_headers[3];
    EXPECT_TRUE(callback(&filter, result_headers));

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
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
