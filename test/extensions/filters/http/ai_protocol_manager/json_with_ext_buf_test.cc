#include <cstdint>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::HasStatusCode;

using ExternalRef = JsonWithExtBuf::ExternalRef;

TEST(JsonWithExtBufTest, ExternalRefRoundTrips) {
  const ExternalRef ref{/*offset=*/1234, /*length=*/5678};
  const nlohmann::json node = JsonWithExtBuf::makeExternalRef(ref);

  EXPECT_TRUE(JsonWithExtBuf::isExternalRef(node));
  const absl::StatusOr<ExternalRef> decoded = JsonWithExtBuf::externalRef(node);
  ASSERT_OK(decoded);
  EXPECT_EQ(*decoded, ref);
}

// Fixed-width: the size does not vary with the values it holds.
TEST(JsonWithExtBufTest, ExternalRefEncodingIsFixedWidth) {
  const nlohmann::json small = JsonWithExtBuf::makeExternalRef({0, 0});
  const nlohmann::json large = JsonWithExtBuf::makeExternalRef({
      /*offset=*/0xfedcba9876543210ULL,
      /*length=*/0x0123456789abcdefULL,
  });

  EXPECT_EQ(small.get_binary().size(), JsonWithExtBuf::kExternalRefBytes);
  EXPECT_EQ(large.get_binary().size(), JsonWithExtBuf::kExternalRefBytes);

  const absl::StatusOr<ExternalRef> decoded = JsonWithExtBuf::externalRef(large);
  ASSERT_OK(decoded);
  EXPECT_EQ(decoded->offset, 0xfedcba9876543210ULL);
  EXPECT_EQ(decoded->length, 0x0123456789abcdefULL);
}

// Nothing in a user payload can be mistaken for a buffer pointer.
TEST(JsonWithExtBufTest, NonReferenceNodesAreNotReferences) {
  const std::vector<nlohmann::json> values = {
      nlohmann::json("a string"), nlohmann::json(42),      nlohmann::json(nullptr),
      nlohmann::json::object(),   nlohmann::json::array(),
  };
  for (const nlohmann::json& value : values) {
    EXPECT_FALSE(JsonWithExtBuf::isExternalRef(value));
    EXPECT_THAT(JsonWithExtBuf::externalRef(value).status(),
                HasStatusCode(absl::StatusCode::kInvalidArgument));
  }
}

// A binary node with someone else's subtype, or none, is not ours.
TEST(JsonWithExtBufTest, ForeignBinarySubtypeIsNotAReference) {
  const std::vector<std::uint8_t> payload(JsonWithExtBuf::kExternalRefBytes, 0);

  EXPECT_FALSE(JsonWithExtBuf::isExternalRef(
      nlohmann::json::binary(payload, JsonWithExtBuf::kExternalRefSubtype + 1)));
  EXPECT_FALSE(JsonWithExtBuf::isExternalRef(nlohmann::json::binary(payload)));
}

// A correctly tagged node of the wrong size is rejected, not read out of bounds.
TEST(JsonWithExtBufTest, TruncatedPayloadIsRejected) {
  const nlohmann::json truncated =
      nlohmann::json::binary(std::vector<std::uint8_t>(JsonWithExtBuf::kExternalRefBytes - 1, 0),
                             JsonWithExtBuf::kExternalRefSubtype);

  EXPECT_TRUE(JsonWithExtBuf::isExternalRef(truncated));
  EXPECT_THAT(JsonWithExtBuf::externalRef(truncated).status(),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST(JsonWithExtBufTest, HoldsTheBufferItsReferencesPointInto) {
  JsonWithExtBuf without_buffer;
  EXPECT_EQ(without_buffer.externalBuffer(), nullptr);
  EXPECT_TRUE(without_buffer.json().is_null());

  // The concrete buffer is irrelevant here; only the association is under test.
  ExternalBuffer* const marker = reinterpret_cast<ExternalBuffer*>(0x1);
  JsonWithExtBuf doc(marker);
  EXPECT_EQ(doc.externalBuffer(), marker);

  doc.setJson(nlohmann::json::parse(R"({"model":"gpt-4"})"));
  EXPECT_EQ(doc.json()["model"], "gpt-4");
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
