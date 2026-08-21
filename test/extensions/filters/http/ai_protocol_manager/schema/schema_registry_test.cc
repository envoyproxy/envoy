#include <algorithm>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/schema/schema_registry.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

TEST(SchemaRegistryTest, SchemaRegistryLookup) {
  const PayloadSchema* schema = SchemaRegistry::getSchema(ApiProtocol::OpenAiChatCompletions);
  ASSERT_NE(schema, nullptr);

  // APIs without a defined payload schema are not validated.
  EXPECT_EQ(SchemaRegistry::getSchema(ApiProtocol::Unspecified), nullptr);
  EXPECT_EQ(SchemaRegistry::getSchema(ApiProtocol::OpenAiResponses), nullptr);
  EXPECT_EQ(SchemaRegistry::getSchema(ApiProtocol::AnthropicMessages), nullptr);
  EXPECT_EQ(SchemaRegistry::getSchema(ApiProtocol::GeminiGenerateContent), nullptr);
}

TEST(SchemaRegistryTest, AllDeclaredOffloadableFieldsInStreamOrder) {
  // Every registered schema must list each of its offloadable field paths in
  // its streamable field order.
  for (const ApiProtocol protocol :
       {ApiProtocol::OpenAiChatCompletions, ApiProtocol::OpenAiResponses,
        ApiProtocol::AnthropicMessages, ApiProtocol::GeminiGenerateContent}) {
    const PayloadSchema* schema = SchemaRegistry::getSchema(protocol);
    if (schema == nullptr) {
      continue;
    }

    const std::vector<std::string> offloadable_paths = schema->requestOffloadableFieldPaths();
    const std::vector<std::string>& stream_order = schema->requestStreamableFieldOrder();

    EXPECT_FALSE(offloadable_paths.empty());
    for (const std::string& offloadable_path : offloadable_paths) {
      EXPECT_NE(std::find(stream_order.begin(), stream_order.end(), offloadable_path),
                stream_order.end())
          << "Declared offloadable field path '" << offloadable_path
          << "' is missing from the streamable field order list for protocol "
          << apiProtocolName(protocol);
    }
  }
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
