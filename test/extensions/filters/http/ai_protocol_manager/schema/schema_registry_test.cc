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
  const PayloadSchema* schema = SchemaRegistry::getSchema(PerRouteProto::OPENAI_CHAT_COMPLETIONS);
  ASSERT_NE(schema, nullptr);

  const PayloadSchema* unspec_schema = SchemaRegistry::getSchema(PerRouteProto::UNSPECIFIED);
  EXPECT_EQ(unspec_schema, nullptr);

  const PayloadSchema* unknown_schema =
      SchemaRegistry::getSchema(static_cast<PerRouteProto::Schema>(999));
  EXPECT_EQ(unknown_schema, nullptr);
}

TEST(SchemaRegistryTest, AllDeclaredOffloadableFieldsInStreamOrder) {
  const auto* descriptor = PerRouteProto::Schema_descriptor();
  ASSERT_NE(descriptor, nullptr);

  for (int i = 0; i < descriptor->value_count(); ++i) {
    const auto* value_descriptor = descriptor->value(i);
    ASSERT_NE(value_descriptor, nullptr);
    const auto schema_enum = static_cast<PerRouteProto::Schema>(value_descriptor->number());
    if (schema_enum == PerRouteProto::UNSPECIFIED) {
      continue;
    }

    const PayloadSchema* schema = SchemaRegistry::getSchema(schema_enum);
    ASSERT_NE(schema, nullptr) << "SchemaRegistry missing registered schema for enum "
                               << value_descriptor->name();

    const std::vector<std::string> offloadable_paths = schema->requestOffloadableFieldPaths();
    const std::vector<std::string>& stream_order = schema->requestStreamableFieldOrder();

    EXPECT_FALSE(offloadable_paths.empty());
    for (const std::string& offloadable_path : offloadable_paths) {
      EXPECT_NE(std::find(stream_order.begin(), stream_order.end(), offloadable_path),
                stream_order.end())
          << "Declared offloadable field path '" << offloadable_path
          << "' is missing from the streamable field order list in schema "
          << value_descriptor->name();
    }
  }
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
