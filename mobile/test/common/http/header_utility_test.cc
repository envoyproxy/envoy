#include "source/common/http/header_map_impl.h"

#include "gtest/gtest.h"
#include "library/common/bridge/utility.h"
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

TEST(RequestHeaderDataConstructorTest, FromCToCppEmpty) {
  RequestHeaderMapPtr cpp_headers = Utility::toRequestHeaders(envoy_headers{});

  ASSERT_TRUE(cpp_headers->empty());
}

TEST(RequestHeaderDataConstructorTest, FromCToCpp) {
  // Backing strings for all the envoy_datas in the c_headers.
  std::vector<std::pair<std::string, std::string>> headers = {
      {":method", "GET"}, {":scheme", "https"}, {":authority", "api.lyft.com"}, {":path", "/ping"}};

  envoy_map_entry* header_array =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * headers.size()));

  uint32_t* sentinel = new uint32_t;
  *sentinel = 0;
  for (size_t i = 0; i < headers.size(); i++) {
    header_array[i] = {
        envoyTestString(headers[i].first, sentinel),
        envoyTestString(headers[i].second, sentinel),
    };
  }

  envoy_headers c_headers = {static_cast<envoy_map_size_t>(headers.size()), header_array};
  // This copy is used for assertions given that envoy_headers are released when toRequestHeaders
  // is called.
  envoy_headers c_headers_copy = copy_envoy_headers(c_headers);

  RequestHeaderMapPtr cpp_headers = Utility::toRequestHeaders(c_headers);

  // Check that the sentinel was advance due to c_headers being released;
  ASSERT_EQ(*sentinel, 2 * c_headers_copy.length);

  ASSERT_EQ(cpp_headers->size(), c_headers_copy.length);

  for (envoy_map_size_t i = 0; i < c_headers_copy.length; i++) {
    auto expected_key =
        LowerCaseString(Bridge::Utility::copyToString(c_headers_copy.entries[i].key));
    auto expected_value = Bridge::Utility::copyToString(c_headers_copy.entries[i].value);

    // Key is present.
    EXPECT_FALSE(cpp_headers->get(expected_key).empty());
    // Value for the key is the same.
    EXPECT_EQ(cpp_headers->get(expected_key)[0]->value().getStringView(), expected_value);
  }
  release_envoy_headers(c_headers_copy);
  delete sentinel;
}

TEST(HeaderDataConstructorTest, FromCppToCEmpty) {
  RequestHeaderMapPtr empty_headers = RequestHeaderMapImpl::create();
  envoy_headers c_headers = Utility::toBridgeHeaders(*empty_headers);
  ASSERT_EQ(0, c_headers.length);
  release_envoy_headers(c_headers);
}

TEST(HeaderDataConstructorTest, FromCppToC) {
  RequestHeaderMapPtr cpp_headers = RequestHeaderMapImpl::create();
  cpp_headers->addCopy(LowerCaseString(std::string(":method")), std::string("GET"));
  cpp_headers->addCopy(LowerCaseString(std::string(":scheme")), std::string("https"));
  cpp_headers->addCopy(LowerCaseString(std::string(":authority")), std::string("api.lyft.com"));
  cpp_headers->addCopy(LowerCaseString(std::string(":path")), std::string("/ping"));

  envoy_headers c_headers = Utility::toBridgeHeaders(*cpp_headers);

  ASSERT_EQ(c_headers.length, static_cast<envoy_map_size_t>(cpp_headers->size()));

  for (envoy_map_size_t i = 0; i < c_headers.length; i++) {
    LowerCaseString actual_key(Bridge::Utility::copyToString(c_headers.entries[i].key));
    std::string actual_value = Bridge::Utility::copyToString(c_headers.entries[i].value);

    // Key is present.
    EXPECT_FALSE(cpp_headers->get(actual_key).empty());
    // Value for the key is the same.
    EXPECT_EQ(actual_value, cpp_headers->get(actual_key)[0]->value().getStringView());
  }

  release_envoy_headers(c_headers);
}

TEST(HeaderDataConstructorTest, FromCppToCWithAlpn) {
  RequestHeaderMapPtr cpp_headers = RequestHeaderMapImpl::create();
  cpp_headers->addCopy(LowerCaseString(std::string(":method")), std::string("GET"));
  cpp_headers->addCopy(LowerCaseString(std::string(":scheme")), std::string("https"));
  cpp_headers->addCopy(LowerCaseString(std::string(":authority")), std::string("api.lyft.com"));
  cpp_headers->addCopy(LowerCaseString(std::string(":path")), std::string("/ping"));

  envoy_headers c_headers = Utility::toBridgeHeaders(*cpp_headers, "h2");

  cpp_headers->addCopy(LowerCaseString(std::string("x-envoy-upstream-alpn")), std::string("h2"));
  ASSERT_EQ(c_headers.length, static_cast<envoy_map_size_t>(cpp_headers->size()));

  for (envoy_map_size_t i = 0; i < c_headers.length; i++) {
    LowerCaseString actual_key(Bridge::Utility::copyToString(c_headers.entries[i].key));
    std::string actual_value = Bridge::Utility::copyToString(c_headers.entries[i].value);

    // Key is present.
    EXPECT_FALSE(cpp_headers->get(actual_key).empty());
    // Value for the key is the same.
    EXPECT_EQ(actual_value, cpp_headers->get(actual_key)[0]->value().getStringView());
  }

  release_envoy_headers(c_headers);
}

} // namespace Http
} // namespace Envoy
