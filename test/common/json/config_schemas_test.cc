#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

#include "test/test_common/environment.h"

#include <dirent.h>

using testing::_;

namespace Json {

std::vector<std::string> generateTestInputs() {
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/common/json/test_data/generate_test_data.py")});

  std::string test_path = TestEnvironment::temporaryDirectory();
  DIR* dir = opendir(test_path.c_str());
  if (!dir) {
    throw std::runtime_error("Generated config schema test directory not found");
  }

  dirent* entry;
  std::vector<std::string> file_names;
  while ((entry = readdir(dir)) != nullptr) {
    std::string file_name = fmt::format("{}/{}", test_path, std::string(entry->d_name));
    if (entry->d_type == DT_DIR) {
      continue;
    }
    file_names.push_back(file_name);
  }
  closedir(dir);

  return file_names;
}

class ConfigSchemasTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ConfigSchemasTest, CheckValidationExpectation) {
  ObjectPtr json = Factory::LoadFromFile(GetParam());

  // lookup schema in test input
  std::string schema, schema_name{json->getString("schema")};
  if (schema_name == "LISTENER_SCHEMA") {
    schema = Schema::LISTENER_SCHEMA;
  } else if (schema_name == "HTTP_CONN_NETWORK_FILTER_SCHEMA") {
    schema = Schema::HTTP_CONN_NETWORK_FILTER_SCHEMA;
  } else {
    FAIL() << fmt::format("Did not recognize schema name {}", schema_name);
  }

  // perform validation and verify result
  if (json->getBoolean("throws", false)) {
    EXPECT_THROW(json->getObject("data")->validateSchema(schema), Exception);
  } else {
    EXPECT_NO_THROW(json->getObject("data")->validateSchema(schema));
  }
}

INSTANTIATE_TEST_CASE_P(Default, ConfigSchemasTest, testing::ValuesIn(generateTestInputs()));
}
