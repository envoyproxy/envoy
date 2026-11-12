#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/matching/input_matchers/dynamic_modules/matcher.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {
namespace {

class DynamicModuleMatcherAbiTest : public testing::Test {
public:
  void SetUp() override {}

  MatchContext createMatchContext() {
    MatchContext context;
    context.request_headers = &request_headers_;
    context.response_headers = &response_headers_;
    context.response_trailers = &response_trailers_;
    return context;
  }

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers_{{"x-request-id", "req-123"},
                                                           {"host", "example.com"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers_{
      {"content-type", "application/json"}, {"x-custom", "value1"}, {"x-custom", "value2"}};
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailers_{{"x-trailer", "trailer-value"}};
};

// =============================================================================
// Header Size Tests
// =============================================================================

TEST_F(DynamicModuleMatcherAbiTest, HeadersSizeRequestHeaders) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  EXPECT_EQ(2, envoy_dynamic_module_callback_matcher_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader));
}

TEST_F(DynamicModuleMatcherAbiTest, HeadersSizeResponseHeaders) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  // 3 headers: content-type, x-custom, x-custom.
  EXPECT_EQ(3, envoy_dynamic_module_callback_matcher_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_ResponseHeader));
}

TEST_F(DynamicModuleMatcherAbiTest, HeadersSizeResponseTrailers) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  EXPECT_EQ(1, envoy_dynamic_module_callback_matcher_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_ResponseTrailer));
}

TEST_F(DynamicModuleMatcherAbiTest, HeadersSizeNullHeaders) {
  MatchContext context;
  context.request_headers = nullptr;
  context.response_headers = nullptr;
  context.response_trailers = nullptr;
  void* env_ptr = static_cast<void*>(&context);

  EXPECT_EQ(0, envoy_dynamic_module_callback_matcher_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader));
  EXPECT_EQ(0, envoy_dynamic_module_callback_matcher_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_ResponseHeader));
  EXPECT_EQ(0, envoy_dynamic_module_callback_matcher_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_ResponseTrailer));
}

// =============================================================================
// Get All Headers Tests
// =============================================================================

TEST_F(DynamicModuleMatcherAbiTest, GetHeaders) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  std::vector<envoy_dynamic_module_type_envoy_http_header> headers(2);
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_get_headers(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, headers.data()));

  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(headers.size());
  for (const auto& h : headers) {
    result.push_back(
        {std::string(h.key_ptr, h.key_length), std::string(h.value_ptr, h.value_length)});
  }
  EXPECT_THAT(result, testing::UnorderedElementsAre(testing::Pair("x-request-id", "req-123"),
                                                    testing::Pair(":authority", "example.com")));
}

TEST_F(DynamicModuleMatcherAbiTest, GetHeadersNull) {
  MatchContext context;
  context.request_headers = nullptr;
  void* env_ptr = static_cast<void*>(&context);

  EXPECT_FALSE(envoy_dynamic_module_callback_matcher_get_headers(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, nullptr));
}

TEST_F(DynamicModuleMatcherAbiTest, GetHeadersResponseTrailers) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  std::vector<envoy_dynamic_module_type_envoy_http_header> headers(1);
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_get_headers(
      env_ptr, envoy_dynamic_module_type_http_header_type_ResponseTrailer, headers.data()));

  EXPECT_EQ("x-trailer", std::string(headers[0].key_ptr, headers[0].key_length));
  EXPECT_EQ("trailer-value", std::string(headers[0].value_ptr, headers[0].value_length));
}

// =============================================================================
// Get Header Value Tests
// =============================================================================

TEST_F(DynamicModuleMatcherAbiTest, GetHeaderValueFound) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_type_module_buffer key = {"x-request-id", 12};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 0, &count));
  EXPECT_EQ("req-123", std::string(result.ptr, result.length));
  EXPECT_EQ(1, count);
}

TEST_F(DynamicModuleMatcherAbiTest, GetHeaderValueMultiValue) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_type_module_buffer key = {"x-custom", 8};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  // Get first value.
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_ResponseHeader, key, &result, 0, &count));
  EXPECT_EQ("value1", std::string(result.ptr, result.length));
  EXPECT_EQ(2, count);

  // Get second value.
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_ResponseHeader, key, &result, 1, &count));
  EXPECT_EQ("value2", std::string(result.ptr, result.length));
  EXPECT_EQ(2, count);
}

TEST_F(DynamicModuleMatcherAbiTest, GetHeaderValueNotFound) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_type_module_buffer key = {"nonexistent", 11};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  EXPECT_FALSE(envoy_dynamic_module_callback_matcher_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 0, &count));
  EXPECT_EQ(0, count);
}

TEST_F(DynamicModuleMatcherAbiTest, GetHeaderValueIndexOutOfBounds) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_type_module_buffer key = {"x-request-id", 12};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  EXPECT_FALSE(envoy_dynamic_module_callback_matcher_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 1, &count));
  EXPECT_EQ(1, count);
}

TEST_F(DynamicModuleMatcherAbiTest, GetHeaderValueNullMap) {
  MatchContext context;
  context.request_headers = nullptr;
  void* env_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_type_module_buffer key = {"x-request-id", 12};
  envoy_dynamic_module_type_envoy_buffer result;
  size_t count = 0;

  EXPECT_FALSE(envoy_dynamic_module_callback_matcher_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 0, &count));
  EXPECT_EQ(0, count);
}

TEST_F(DynamicModuleMatcherAbiTest, GetHeaderValueNullTotalCount) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  envoy_dynamic_module_type_module_buffer key = {"x-request-id", 12};
  envoy_dynamic_module_type_envoy_buffer result;

  // Pass nullptr for total_count_out - should not crash.
  EXPECT_TRUE(envoy_dynamic_module_callback_matcher_get_header_value(
      env_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, key, &result, 0, nullptr));
  EXPECT_EQ("req-123", std::string(result.ptr, result.length));
}

// =============================================================================
// Invalid Header Type Tests
// =============================================================================

TEST_F(DynamicModuleMatcherAbiTest, InvalidHeaderType) {
  auto context = createMatchContext();
  void* env_ptr = static_cast<void*>(&context);

  // Request trailers are not provided by the matcher data input.
  EXPECT_EQ(0, envoy_dynamic_module_callback_matcher_get_headers_size(
                   env_ptr, envoy_dynamic_module_type_http_header_type_RequestTrailer));
}

} // namespace
} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
