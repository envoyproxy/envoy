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
      callbacks = {envoy_dynamic_module_callback_http_get_request_header_value,
                   envoy_dynamic_module_callback_http_get_request_trailer_value,
                   envoy_dynamic_module_callback_http_get_response_header_value,
                   envoy_dynamic_module_callback_http_get_response_trailer_value};

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

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
