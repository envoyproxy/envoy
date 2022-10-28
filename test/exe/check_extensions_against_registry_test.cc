#include "envoy/registry/registry.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

std::vector<std::string> stringsFromListValue(const ProtobufWkt::Value& value) {
  std::vector<std::string> strings;
  for (const auto& elt : value.list_value().values()) {
    strings.push_back(elt.string_value());
  }
  return strings;
}

// Ensure that the type URLs in the static extension schema match the type URLs
// in the internal extension registry.
TEST(CheckExtensionsAgainstRegistry, CorrectMetadata) {
  // Manifest schema example:
  // <extension_name>:
  //   categories:
  //   - <category_name>
  //   type_urls:
  //   - <sorted type_url>
  const std::string manifest_path =
      TestEnvironment::runfilesPath("source/extensions/extensions_metadata.yaml");
  const std::string manifest = TestEnvironment::readFileToStringForTest(manifest_path);
  ProtobufWkt::Value value = ValueUtil::loadFromYaml(manifest);
  ASSERT_EQ(ProtobufWkt::Value::kStructValue, value.kind_case());
  const auto& json = value.struct_value();

  for (const auto& ext : Registry::FactoryCategoryRegistry::registeredFactories()) {
    auto registered_types = ext.second->registeredTypes();
    for (const auto& name : ext.second->allRegisteredNames()) {
      if (ext.second->canonicalFactoryName(name) != name) {
        continue;
      }
      const auto& it = json.fields().find(std::string(name));
      if (it == json.fields().end()) {
        ENVOY_LOG_MISC(warn, "Missing extension '{}' from category '{}'.", name, ext.first);
        continue;
      }
      ASSERT_EQ(ProtobufWkt::Value::kStructValue, it->second.kind_case())
          << "Malformed extension metadata for: " << name;
      const auto& extension_fields = it->second.struct_value().fields();

      // Validate that the extension category from the registry is listed.
      const auto& categories_it = extension_fields.find("categories");
      ASSERT_TRUE(categories_it != extension_fields.end())
          << "Missing field 'categories' for: " << name;
      EXPECT_THAT(stringsFromListValue(categories_it->second), ::testing::Contains(ext.first))
          << "Missing category: " << name;

      // Validate that the type URLs from the registry are listed.
      std::vector<std::string> type_urls;
      const auto& type_urls_it = extension_fields.find("type_urls");
      if (type_urls_it != extension_fields.end()) {
        type_urls = stringsFromListValue(type_urls_it->second);
      }

      const auto& expected_types = registered_types.find(name);
      if (expected_types != registered_types.end()) {
        std::sort(expected_types->second.begin(), expected_types->second.end());
        EXPECT_THAT(type_urls, ::testing::IsSupersetOf(expected_types->second))
            << "Mismatched type URLs: " << name;
      } else {
        EXPECT_EQ(type_urls.size(), 0);
      }
    }
  }
}

} // namespace
} // namespace Envoy
