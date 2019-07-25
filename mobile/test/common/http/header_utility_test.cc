#include "common/http/header_map_impl.h"

#include "gtest/gtest.h"
#include "library/common/http/header_utility.h"
#include "library/common/include/c_types.h"

namespace Envoy {
namespace Http {

envoy_data envoyString(std::string& s) {
  return {s.size(), reinterpret_cast<const uint8_t*>(s.c_str())};
}

TEST(HeaderDataConstructorTest, FromCToCppEmpty) {
  envoy_header* header_array = new envoy_header[0];
  envoy_headers empty_headers = {0, header_array};

  HeaderMapPtr cpp_headers = Utility::transformHeaders(empty_headers);

  ASSERT_TRUE(cpp_headers->empty());
  delete[] header_array;
}

TEST(HeaderDataConstructorTest, FromCToCpp) {
  // Backing strings for all the envoy_datas in the c_headers.
  std::vector<std::pair<std::string, std::string>> headers = {
      {":method", "GET"}, {":scheme", "https"}, {":authority", "api.lyft.com"}, {":path", "/ping"}};

  envoy_header* header_array = new envoy_header[headers.size()];

  for (size_t i = 0; i < headers.size(); i++) {
    header_array[i] = {
        envoyString(headers[i].first),
        envoyString(headers[i].second),
    };
  }

  envoy_headers c_headers = {headers.size(), header_array};

  HeaderMapPtr cpp_headers = Utility::transformHeaders(c_headers);

  ASSERT_EQ(cpp_headers->size(), c_headers.length);

  for (uint64_t i = 0; i < c_headers.length; i++) {
    auto expected_key = LowerCaseString(Utility::convertToString(c_headers.headers[i].key));
    auto expected_value = Utility::convertToString(c_headers.headers[i].value);

    // Key is present.
    EXPECT_NE(cpp_headers->get(expected_key), nullptr);
    // Value for the key is the same.
    EXPECT_EQ(cpp_headers->get(expected_key)->value().getStringView(), expected_value);
  }
}

TEST(HeaderDataConstructorTest, FromCppToCEmpty) {
  HeaderMapImpl empty_headers;
  envoy_headers c_headers = Utility::transformHeaders(std::move(empty_headers));
  ASSERT_EQ(0, c_headers.length);
  delete[] c_headers.headers;
}

TEST(HeaderDataConstructorTest, FromCppToC) {
  HeaderMapImpl cpp_headers;
  cpp_headers.addCopy(LowerCaseString(std::string(":method")), std::string("GET"));
  cpp_headers.addCopy(LowerCaseString(std::string(":scheme")), std::string("https"));
  cpp_headers.addCopy(LowerCaseString(std::string(":authority")), std::string("api.lyft.com"));
  cpp_headers.addCopy(LowerCaseString(std::string(":path")), std::string("/ping"));

  envoy_headers c_headers = Utility::transformHeaders(std::move(cpp_headers));

  ASSERT_EQ(c_headers.length, cpp_headers.size());

  for (uint64_t i = 0; i < c_headers.length; i++) {
    auto actual_key = LowerCaseString(Utility::convertToString(c_headers.headers[i].key));
    auto actual_value = Utility::convertToString(c_headers.headers[i].value);

    // Key is present.
    EXPECT_NE(cpp_headers.get(actual_key), nullptr);
    // Value for the key is the same.
    EXPECT_EQ(actual_value, cpp_headers.get(actual_key)->value().getStringView());
  }

  delete c_headers.headers;
}

} // namespace Http
} // namespace Envoy
