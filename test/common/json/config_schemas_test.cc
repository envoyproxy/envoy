#include <string>
#include <vector>

#include "common/common/fmt.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Json {

std::vector<std::string> generateTestInputs() {
  TestEnvironment::exec({TestEnvironment::runfilesPath(
      "test/common/json/config_schemas_test_data/generate_test_data")});

  std::string test_path = TestEnvironment::temporaryDirectory() + "/config_schemas_test";
  auto file_list = TestUtility::listFiles(test_path, false);
  EXPECT_EQ(21, file_list.size());
  return file_list;
}

class ConfigSchemasTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ConfigSchemasTest, CheckValidationExpectation) {
  ObjectSharedPtr json = Factory::loadFromFile(GetParam());

  // lookup schema in test input
  std::string schema, schema_name{json->getString("schema")};
  if (schema_name == "LISTENER_SCHEMA") {
    schema = Schema::LISTENER_SCHEMA;
  } else if (schema_name == "HTTP_CONN_NETWORK_FILTER_SCHEMA") {
    schema = Schema::HTTP_CONN_NETWORK_FILTER_SCHEMA;
  } else if (schema_name == "ROUTER_HTTP_FILTER_SCHEMA") {
    schema = Schema::ROUTER_HTTP_FILTER_SCHEMA;
  } else if (schema_name == "ROUTE_CONFIGURATION_SCHEMA") {
    schema = Schema::ROUTE_CONFIGURATION_SCHEMA;
  } else if (schema_name == "ROUTE_ENTRY_CONFIGURATION_SCHEMA") {
    schema = Schema::ROUTE_ENTRY_CONFIGURATION_SCHEMA;
  } else if (schema_name == "CLUSTER_SCHEMA") {
    schema = Schema::CLUSTER_SCHEMA;
  } else if (schema_name == "TOP_LEVEL_CONFIG_SCHEMA") {
    schema = Schema::TOP_LEVEL_CONFIG_SCHEMA;
  } else if (schema_name == "ACCESS_LOG_SCHEMA") {
    schema = Schema::ACCESS_LOG_SCHEMA;
  } else {
    FAIL() << fmt::format("Did not recognize schema name {}", schema_name);
  }

  // perform validation and verify result
  // TODO(danielhochman): add support for EXPECT_THROW_WITH_MESSAGE
  if (json->getBoolean("throws", false)) {
    EXPECT_THROW(json->getObject("data")->validateSchema(schema), Exception);
  } else {
    json->getObject("data")->validateSchema(schema);
  }
}

INSTANTIATE_TEST_CASE_P(Default, ConfigSchemasTest, testing::ValuesIn(generateTestInputs()));
} // namespace Json
} // namespace Envoy
