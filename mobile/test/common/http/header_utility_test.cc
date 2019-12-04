#include "common/http/header_map_impl.h"

#include "gtest/gtest.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Http {

void envoy_test_release(void* context) {
  uint32_t* counter = static_cast<uint32_t*>(context);
  *counter = *counter + 1;
}

envoy_data envoyTestString(std::string& s, uint32_t* sentinel) {
  return {s.size(), reinterpret_cast<const uint8_t*>(s.c_str()), envoy_test_release, sentinel};
}

TEST(HeaderDataConstructorTest, FromCToCppEmpty) {
  envoy_header* header_array = new envoy_header[0];
  envoy_headers empty_headers = {0, header_array};

  HeaderMapPtr cpp_headers = Utility::toInternalHeaders(empty_headers);

  ASSERT_TRUE(cpp_headers->empty());
}

TEST(HeaderDataConstructorTest, FromCToCpp) {
  // Backing strings for all the envoy_datas in the c_headers.
  std::vector<std::pair<std::string, std::string>> headers = {
      {":method", "GET"}, {":scheme", "https"}, {":authority", "api.lyft.com"}, {":path", "/ping"}};

  envoy_header* header_array = new envoy_header[headers.size()];

  uint32_t* sentinel = new uint32_t;
  *sentinel = 0;
  for (size_t i = 0; i < headers.size(); i++) {
    header_array[i] = {
        envoyTestString(headers[i].first, sentinel),
        envoyTestString(headers[i].second, sentinel),
    };
  }

  envoy_headers c_headers = {static_cast<envoy_header_size_t>(headers.size()), header_array};
  // This copy is used for assertions given that envoy_headers are released when toInternalHeaders
  // is called.
  envoy_headers c_headers_copy = copy_envoy_headers(c_headers);

  HeaderMapPtr cpp_headers = Utility::toInternalHeaders(c_headers);

  // Check that the sentinel was advance due to c_headers being released;
  ASSERT_EQ(*sentinel, 2 * c_headers_copy.length);

  ASSERT_EQ(cpp_headers->size(), c_headers_copy.length);

  for (envoy_header_size_t i = 0; i < c_headers_copy.length; i++) {
    auto expected_key = LowerCaseString(Utility::convertToString(c_headers_copy.headers[i].key));
    auto expected_value = Utility::convertToString(c_headers_copy.headers[i].value);

    // Key is present.
    EXPECT_NE(cpp_headers->get(expected_key), nullptr);
    // Value for the key is the same.
    EXPECT_EQ(cpp_headers->get(expected_key)->value().getStringView(), expected_value);
  }
  release_envoy_headers(c_headers_copy);
  delete sentinel;
}

TEST(HeaderDataConstructorTest, FromCppToCEmpty) {
  HeaderMapImpl empty_headers;
  envoy_headers c_headers = Utility::toBridgeHeaders(std::move(empty_headers));
  ASSERT_EQ(0, c_headers.length);
  release_envoy_headers(c_headers);
}

TEST(HeaderDataConstructorTest, FromCppToC) {
  HeaderMapImpl cpp_headers;
  cpp_headers.addCopy(LowerCaseString(std::string(":method")), std::string("GET"));
  cpp_headers.addCopy(LowerCaseString(std::string(":scheme")), std::string("https"));
  cpp_headers.addCopy(LowerCaseString(std::string(":authority")), std::string("api.lyft.com"));
  cpp_headers.addCopy(LowerCaseString(std::string(":path")), std::string("/ping"));

  envoy_headers c_headers = Utility::toBridgeHeaders(std::move(cpp_headers));

  ASSERT_EQ(c_headers.length, static_cast<envoy_header_size_t>(cpp_headers.size()));

  for (envoy_header_size_t i = 0; i < c_headers.length; i++) {
    auto actual_key = LowerCaseString(Utility::convertToString(c_headers.headers[i].key));
    auto actual_value = Utility::convertToString(c_headers.headers[i].value);

    // Key is present.
    EXPECT_NE(cpp_headers.get(actual_key), nullptr);
    // Value for the key is the same.
    EXPECT_EQ(actual_value, cpp_headers.get(actual_key)->value().getStringView());
  }

  release_envoy_headers(c_headers);
}

} // namespace Http
} // namespace Envoy
