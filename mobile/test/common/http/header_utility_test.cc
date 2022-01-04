#include "source/common/http/header_map_impl.h"

#include "gtest/gtest.h"
#include "library/common/data/utility.h"
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
  envoy_map_entry* header_array = new envoy_map_entry[0];
  envoy_headers empty_headers = {0, header_array};

  RequestHeaderMapPtr cpp_headers = Utility::toRequestHeaders(empty_headers);

  ASSERT_TRUE(cpp_headers->empty());
}

TEST(RequestTrailerDataConstructorTest, FromCToCppEmpty) {
  envoy_map_entry* header_array = new envoy_map_entry[0];
  envoy_headers empty_trailers = {0, header_array};

  RequestTrailerMapPtr cpp_trailers = Utility::toRequestTrailers(empty_trailers);

  ASSERT_TRUE(cpp_trailers->empty());
}

TEST(RequestHeaderDataConstructorTest, FromCToCpp) {
  // Backing strings for all the envoy_datas in the c_headers.
  std::vector<std::pair<std::string, std::string>> headers = {
      {":method", "GET"}, {":scheme", "https"}, {":authority", "api.lyft.com"}, {":path", "/ping"}};

  envoy_map_entry* header_array = new envoy_map_entry[headers.size()];

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
    auto expected_key = LowerCaseString(Data::Utility::copyToString(c_headers_copy.entries[i].key));
    auto expected_value = Data::Utility::copyToString(c_headers_copy.entries[i].value);

    // Key is present.
    EXPECT_FALSE(cpp_headers->get(expected_key).empty());
    // Value for the key is the same.
    EXPECT_EQ(cpp_headers->get(expected_key)[0]->value().getStringView(), expected_value);
  }
  release_envoy_headers(c_headers_copy);
  delete sentinel;
}

TEST(RequestTrailerDataConstructorTest, FromCToCpp) {
  // Backing strings for all the envoy_datas in the c_trailers.
  std::vector<std::pair<std::string, std::string>> trailers = {
      {"processing-duration-ms", "25"}, {"response-compression-ratio", "0.61"}};

  envoy_map_entry* header_array = new envoy_map_entry[trailers.size()];

  uint32_t* sentinel = new uint32_t;
  *sentinel = 0;
  for (size_t i = 0; i < trailers.size(); i++) {
    header_array[i] = {
        envoyTestString(trailers[i].first, sentinel),
        envoyTestString(trailers[i].second, sentinel),
    };
  }

  envoy_headers c_trailers = {static_cast<envoy_map_size_t>(trailers.size()), header_array};
  // This copy is used for assertions given that envoy_trailers are released when toRequestTrailers
  // is called.
  envoy_headers c_trailers_copy = copy_envoy_headers(c_trailers);

  RequestTrailerMapPtr cpp_trailers = Utility::toRequestTrailers(c_trailers);

  // Check that the sentinel was advance due to c_trailers being released;
  ASSERT_EQ(*sentinel, 2 * c_trailers_copy.length);

  ASSERT_EQ(cpp_trailers->size(), c_trailers_copy.length);

  for (envoy_map_size_t i = 0; i < c_trailers_copy.length; i++) {
    auto expected_key =
        LowerCaseString(Data::Utility::copyToString(c_trailers_copy.entries[i].key));
    auto expected_value = Data::Utility::copyToString(c_trailers_copy.entries[i].value);

    // Key is present.
    EXPECT_FALSE(cpp_trailers->get(expected_key).empty());
    // Value for the key is the same.
    EXPECT_EQ(cpp_trailers->get(expected_key)[0]->value().getStringView(), expected_value);
  }
  release_envoy_headers(c_trailers_copy);
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
    auto actual_key = LowerCaseString(Data::Utility::copyToString(c_headers.entries[i].key));
    auto actual_value = Data::Utility::copyToString(c_headers.entries[i].value);

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
    auto actual_key = LowerCaseString(Data::Utility::copyToString(c_headers.entries[i].key));
    auto actual_value = Data::Utility::copyToString(c_headers.entries[i].value);

    // Key is present.
    EXPECT_FALSE(cpp_headers->get(actual_key).empty());
    // Value for the key is the same.
    EXPECT_EQ(actual_value, cpp_headers->get(actual_key)[0]->value().getStringView());
  }

  release_envoy_headers(c_headers);
}

} // namespace Http
} // namespace Envoy
