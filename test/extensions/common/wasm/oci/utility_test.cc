#include "source/extensions/common/wasm/oci/utility.h"

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

} // namespace
} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
