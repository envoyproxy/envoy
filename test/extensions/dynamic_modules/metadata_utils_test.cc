#include "source/extensions/dynamic_modules/metadata_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

TEST(MetadataUtilsTest, FlatStringValue) {
  envoy::config::core::v3::Metadata metadata;
  Protobuf::Struct struct_obj;
  (*struct_obj.mutable_fields())["method"] = ValueUtil::stringValue("tools/call");
  (*metadata.mutable_filter_metadata())["mcp_filter"] = struct_obj;

  envoy_dynamic_module_type_module_buffer filter = {"mcp_filter", 10};
  envoy_dynamic_module_type_module_buffer path = {"method", 6};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};

  ASSERT_TRUE(getDynamicMetadataStringByPath(metadata, filter, path, &result));
  EXPECT_EQ("tools/call", std::string(result.ptr, result.length));
}

TEST(MetadataUtilsTest, NestedDottedPath) {
  envoy::config::core::v3::Metadata metadata;
  auto& fields = *(*metadata.mutable_filter_metadata())["mcp_filter"].mutable_fields();
  (*fields["params"].mutable_struct_value()->mutable_fields())["protocolVersion"].set_string_value(
      "2025-11-25");

  envoy_dynamic_module_type_module_buffer filter = {"mcp_filter", 10};
  envoy_dynamic_module_type_module_buffer path = {"params.protocolVersion", 22};
  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};

  ASSERT_TRUE(getDynamicMetadataStringByPath(metadata, filter, path, &result));
  EXPECT_EQ("2025-11-25", std::string(result.ptr, result.length));
}

TEST(MetadataUtilsTest, MissingNamespace) {
  envoy::config::core::v3::Metadata metadata;
  envoy_dynamic_module_type_module_buffer filter = {"no_such_filter", 14};
  envoy_dynamic_module_type_module_buffer path = {"key", 3};
  envoy_dynamic_module_type_envoy_buffer result{};

  EXPECT_FALSE(getDynamicMetadataStringByPath(metadata, filter, path, &result));
}

TEST(MetadataUtilsTest, NonStringValue) {
  envoy::config::core::v3::Metadata metadata;
  Protobuf::Struct struct_obj;
  (*struct_obj.mutable_fields())["count"] = ValueUtil::numberValue(42.0);
  (*metadata.mutable_filter_metadata())["mcp_filter"] = struct_obj;

  envoy_dynamic_module_type_module_buffer filter = {"mcp_filter", 10};
  envoy_dynamic_module_type_module_buffer path = {"count", 5};
  envoy_dynamic_module_type_envoy_buffer result{};

  EXPECT_FALSE(getDynamicMetadataStringByPath(metadata, filter, path, &result));
}

TEST(MetadataUtilsTest, MissingNestedKey) {
  envoy::config::core::v3::Metadata metadata;
  auto& fields = *(*metadata.mutable_filter_metadata())["mcp_filter"].mutable_fields();
  (*fields["params"].mutable_struct_value()->mutable_fields())["name"].set_string_value("myTool");

  envoy_dynamic_module_type_module_buffer filter = {"mcp_filter", 10};
  envoy_dynamic_module_type_module_buffer path = {"params.nonExistent", 18};
  envoy_dynamic_module_type_envoy_buffer result{};

  EXPECT_FALSE(getDynamicMetadataStringByPath(metadata, filter, path, &result));
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
