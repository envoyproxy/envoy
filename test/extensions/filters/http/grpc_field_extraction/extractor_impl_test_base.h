#ifndef EXPERIMENTAL_TAOXUY_EXTRACTOR_TEST_BASE_H_
#define EXPERIMENTAL_TAOXUY_EXTRACTOR_TEST_BASE_H_

#include "absl/strings/string_view.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {
namespace testing {

// Service configs for test.
constexpr absl::string_view kCpeScTxtpbPath =
    "google3/apiserving/cloudesf/tests/e2e/endpoints/apikey_grpc/"
    "cap-e2e-codelabapikeys_service.txt";

constexpr absl::string_view kCpeDescriptorPath = "google3/apiserving/cloudesf/tests/e2e/endpoints/"
                                                 "apikey_grpc/cap-e2e-codelabapikeys/descriptors";

// Create method selector to test.
constexpr absl::string_view kCpeSelector = "google.example.apikeys.v1.ApiKeys.CreateApiKey";

// List method selector to test.
constexpr absl::string_view kCpeListApiKeysSelector =
    "google.example.apikeys.v1.ApiKeys.ListApiKeys";

// Runtime list ApiKeys request body.
constexpr absl::string_view kCpeListApiKeysRequestBody = R"pb(
  parent: "projects/cloud-api-proxy-test-client"
)pb";

// Runtime list ApiKeys response body.
constexpr absl::string_view kCpeListApiKeysResponseBody = R"pb(
  keys { name: "key1" display_name: "my-api-key1" }
  keys { name: "key2" display_name: "my-api-key2" }
  keys { name: "key3" display_name: "my-api-key3" }
)pb";

// Runtime create ApiKey request body.
constexpr absl::string_view kCpeRequestBody = R"pb(
  parent: "projects/cloud-api-proxy-test-client"
  key { name: "key" display_name: "my-api-key" }
)pb";

// Runtime create ApiKey request body with missing display name.
constexpr absl::string_view kCpePartialRequestBody = R"pb(
  parent: "projects/cloud-api-proxy-test-client"
  key { name: "key" }
)pb";

// Runtime create ApiKey request body with location in parent name.
constexpr absl::string_view kLocationInParentRequestBody = R"pb(
  parent: "projects/cloud-api-proxy-test-client/locations/us-central1"
  key { name: "key" display_name: "my-api-key" }
)pb";

// Runtime create ApiKey request body with location in other field.
constexpr absl::string_view kLocationInFieldRequestBody = R"pb(
  parent: "projects/cloud-api-proxy-test-client"
  key { name: "key" display_name: "us-central1" }
)pb";

// Runtime create ApiKey response body.
constexpr absl::string_view kCpeResponseBody = R"pb(
  name: "key"
  display_name: "my-api-key"
)pb";

} // namespace testing
} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
