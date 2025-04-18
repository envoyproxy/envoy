#include "source/extensions/common/wasm/oci/utility.h"

#include "absl/strings/match.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {
namespace {

class UtilityTest : public testing::Test {};

TEST_F(UtilityTest, ParseImageUriHttpUriFailure) {
  std::string registry, image_name, tag;
  auto status =
      Oci::parseImageURI("http://example.com/image-name:latest", registry, image_name, tag);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "Failed to parse image URI - unsupported scheme");
}

TEST_F(UtilityTest, ParseImageUriNoImageNameFailure) {
  std::string registry, image_name, tag;
  auto status = Oci::parseImageURI("oci://example.com", registry, image_name, tag);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "Failed to parse image name - URI does not include '/'");
}

TEST_F(UtilityTest, ParseImageUriNoTagFailure) {
  std::string registry, image_name, tag;
  auto status = Oci::parseImageURI("oci://example.com/image-name", registry, image_name, tag);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "Failed to parse image tag - URI does not include ':'");
}

TEST_F(UtilityTest, ParseImageUriSuccess) {
  std::string registry, image_name, tag;
  auto status =
      Oci::parseImageURI("oci://example.com/namespace/repo-name:latest", registry, image_name, tag);
  EXPECT_EQ(status.ok(), true);
  EXPECT_EQ(registry, "example.com");
  EXPECT_EQ(image_name, "namespace/repo-name");
  EXPECT_EQ(tag, "latest");
}

TEST_F(UtilityTest, PrepareAuthorizationHeaderEmptySecretFailure) {
  auto status = Oci::prepareAuthorizationHeader("", "example.com");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.status().message(), "Empty image pull secret");
}

TEST_F(UtilityTest, PrepareAuthorizationHeaderMalformedSecretFailure) {
  auto status = Oci::prepareAuthorizationHeader("{", "example.com");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(absl::StartsWith(status.status().message(), "Failed to parse image pull secret: "));
}

TEST_F(UtilityTest, PrepareAuthorizationHeaderMissingAuthsKeyFailure) {
  auto status = Oci::prepareAuthorizationHeader("{}", "example.com");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(absl::StartsWith(status.status().message(), "Did not find 'auths' key in the image pull secret: "));
}

TEST_F(UtilityTest, PrepareAuthorizationHeaderMissingRegistryKeyFailure) {
  const std::string image_pull_secret = R"EOF({
  "auths": {
    "registry.com":{}
  }
})EOF";
  auto status = Oci::prepareAuthorizationHeader(image_pull_secret, "example.com");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(absl::StartsWith(status.status().message(), "Did not find registry 'example.com' in the image pull secret: "));
}

TEST_F(UtilityTest, PrepareAuthorizationHeaderMissingAuthKeyFailure) {
  const std::string image_pull_secret = R"EOF({
  "auths": {
    "registry.com":{},
    "example.com":{}
  }
})EOF";
  auto status = Oci::prepareAuthorizationHeader(image_pull_secret, "example.com");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.status().message(), "Did not find 'auth' key for registry 'example.com' in the image pull secret");
}

TEST_F(UtilityTest, PrepareAuthorizationHeaderSuccess) {
  const std::string image_pull_secret = R"EOF({
  "auths": {
    "registry.com":{},
    "example.com":{
      "auth": "credential"
    }
  }
})EOF";
  auto status = Oci::prepareAuthorizationHeader(image_pull_secret, "example.com");
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(status.value(), "Basic credential");
}

} // namespace
} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
