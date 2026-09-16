#include <type_traits>
#include <utility>

#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

// The single-ownership contract the handoff queues rely on.
static_assert(!std::is_copy_constructible_v<AiRequest>);
static_assert(!std::is_copy_assignable_v<AiRequest>);
static_assert(!std::is_move_constructible_v<AiRequest>);
static_assert(!std::is_move_assignable_v<AiRequest>);

// json() hands out the stored document itself, so a filter's edit is what serialization
// later reads.
TEST(AiRequestTest, JsonAliasesTheIndex) {
  JsonWithExtBuf index;
  index.setJson(nlohmann::json{{"model", "gpt-4"}});
  AiRequest request(std::move(index));

  EXPECT_EQ(request.json()["model"], "gpt-4");

  request.json()["model"] = "gpt-4-turbo";
  EXPECT_EQ(request.request_index().json()["model"], "gpt-4-turbo");
  EXPECT_EQ(&request.json(), &request.request_index().json());

  const AiRequest& const_request = request;
  EXPECT_EQ(&const_request.json(), &request.json());
}

// A transcoding filter swaps the payload wholesale rather than editing fields.
TEST(AiRequestTest, AssigningJsonReplacesTheDocument) {
  JsonWithExtBuf index;
  index.setJson(nlohmann::json{{"model", "gpt-4"}, {"temperature", 0.5}});
  AiRequest request(std::move(index));

  request.json() = nlohmann::json{{"model", "claude-opus-4"}};

  EXPECT_EQ(request.json()["model"], "claude-opus-4");
  EXPECT_FALSE(request.request_index().json().contains("temperature"));
}

// Offloaded values reach a filter as reference nodes, not as bytes.
TEST(AiRequestTest, ExternalRefsSurviveTheWrapper) {
  const JsonWithExtBuf::ExternalRef ref{/*offset=*/64, /*length=*/4096};
  JsonWithExtBuf index;
  index.setJson(nlohmann::json{{"prompt", JsonWithExtBuf::makeExternalRef(ref)}});
  AiRequest request(std::move(index));

  const nlohmann::json& prompt = request.json()["prompt"];
  ASSERT_TRUE(JsonWithExtBuf::isExternalRef(prompt));
  const absl::StatusOr<JsonWithExtBuf::ExternalRef> decoded = JsonWithExtBuf::externalRef(prompt);
  ASSERT_OK(decoded);
  EXPECT_EQ(*decoded, ref);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
