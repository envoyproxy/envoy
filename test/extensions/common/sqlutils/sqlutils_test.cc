#include "extensions/common/sqlutils/sqlutils.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace SQLUtils {

// MetadataFromSQLTest class is used for parameterized tests.
// The values in the tests are:
// std::string - SQL query
// bool - whether to expect SQL parsing to be successful
// std::map<std::string, std::list<std::string>> map of expected tables accessed based on the query.
// The map is checked only when parsing was successful. Map is indexed by table name and points to
// list of operations performed on the table. For example table1: "select", "insert" says that there
// was SELECT and INSERT operations on table1.
class MetadataFromSQLTest
    : public ::testing::TestWithParam<
          std::tuple<std::string, bool, std::map<std::string, std::list<std::string>>>> {};

// Test takes SQL query as a parameter and checks if the parsing
// produces the correct metadata.
// Metadata is 2-level structure. First layer is list of resources
// over which the SQL query operates: in our case is list of tables.
// Under each table there is secondary list which contains operations performed
// on the table, like "select", "insert", etc.
TEST_P(MetadataFromSQLTest, ParsingAndMetadataTest) {
  // Get the SQL query
  const std::string& query = std::get<0>(GetParam());
  // vector of queries to check.
  std::vector<std::string> test_queries;
  test_queries.push_back(query);

  // Create uppercase and lowercase versions of the queries and put
  // them into vector of queries to check
  test_queries.push_back(absl::AsciiStrToLower(query));
  test_queries.push_back(absl::AsciiStrToUpper(query));

  while (!test_queries.empty()) {
    std::string test_query = test_queries.back();
    ProtobufWkt::Struct metadata;

    // Check if the parsing result is what expected.
    ASSERT_EQ(std::get<1>(GetParam()), SQLUtils::setMetadata(test_query, metadata));

    // If parsing was expected to fail do not check parsing values.
    if (!std::get<1>(GetParam())) {
      return;
    }

    // Access metadata fields, where parsing results are stored.
    auto& fields = *metadata.mutable_fields();

    // Get the names of resources which SQL query operates on.
    std::map<std::string, std::list<std::string>> expected_tables = std::get<2>(GetParam());
    // Check if query results return the same number of resources as expected.
    ASSERT_EQ(expected_tables.size(), fields.size());
    for (const auto& i : fields) {
      // Get from created metadata the list of operations on the resource
      const auto& operations = i;
      std::string table_name = operations.first;

      std::transform(table_name.begin(), table_name.end(), table_name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      // Get the list of expected operations on the same resource from test param.
      const auto& table_name_it = expected_tables.find(table_name);
      // Make sure that a resource (table) found in metadata is expected.
      ASSERT_NE(expected_tables.end(), table_name_it);
      auto& operations_list = table_name_it->second;
      // The number of expected operations and created in metadata must be the same.
      ASSERT_EQ(operations_list.size(), operations.second.list_value().values().size());
      // Now iterate over the operations list found in metadata and check if the same operation
      // is listed as expected in test param.
      for (const auto& j : operations.second.list_value().values()) {
        // Find that operation in test params.
        const auto operation_it =
            std::find(operations_list.begin(), operations_list.end(), j.string_value());
        ASSERT_NE(operations_list.end(), operation_it);
        // Erase the operation. At the end of the test this list should be empty what means
        // that we found all expected operations.
        operations_list.erase(operation_it);
      }
      // Make sure that we went through all expected operations.
      ASSERT_TRUE(operations_list.empty());
      // Remove the table from the list. At the end of the test this list must be empty.
      expected_tables.erase(table_name_it);
    }

    ASSERT_TRUE(expected_tables.empty());
    test_queries.pop_back();
  }
}

// Note: This parameterized test's queries are converted to all lowercase and all uppercase
// to validate that parser is case-insensitive. The test routine converts to uppercase and
// lowercase entire query string, not only SQL keywords. This introduces a problem when comparing
// tables' names when verifying parsing result. Therefore the test converts table names to lowercase
// before comparing. It however requires that all table names in the queries below use lowercase
// only.
#define TEST_VALUE(...)                                                                            \
  std::tuple<std::string, bool, std::map<std::string, std::list<std::string>>> { __VA_ARGS__ }
INSTANTIATE_TEST_SUITE_P(
    SQLUtilsTestSuite, MetadataFromSQLTest,
    ::testing::Values(
        TEST_VALUE("blahblah;", false, {}),

        TEST_VALUE("CREATE TABLE IF NOT EXISTS table1(Usr VARCHAR(40),Count INT);", true,
                   {{"table1", {"create"}}}),
        TEST_VALUE("CREATE TABLE IF NOT EXISTS `table number 1`(Usr VARCHAR(40),Count INT);", true,
                   {{"table number 1", {"create"}}}),
        TEST_VALUE(
            "CREATE TABLE IF NOT EXISTS table1(Usr VARCHAR(40),Count INT); SELECT * from table1;",
            true, {{"table1", {"select", "create"}}}),
        TEST_VALUE(
            "CREATE TABLE IF NOT EXISTS table1(Usr VARCHAR(40),Count INT); SELECT * from table2;",
            true, {{"table1", {"create"}}, {"table2", {"select"}}}),

        TEST_VALUE("CREATE TABLE table1(Usr VARCHAR(40),Count INT);", true,
                   {{"table1", {"create"}}}),
        TEST_VALUE("CREATE TABLE;", false, {}),
        TEST_VALUE("CREATE TEMPORARY table table1(Usr VARCHAR(40),Count INT);", true,
                   {{"table1", {"create"}}}),
        TEST_VALUE("DROP TABLE IF EXISTS table1", true, {{"table1", {"drop"}}}),
        TEST_VALUE("ALTER TABLE table1 add column Id varchar (20);", true, {{"table1", {"alter"}}}),
        TEST_VALUE("INSERT INTO table1 (Usr, Count) VALUES ('allsp2', 3);", true,
                   {{"table1", {"insert"}}}),
        TEST_VALUE("INSERT LOW_PRIORITY INTO table1 (Usr, Count) VALUES ('allsp2', 3);", true,
                   {{"table1", {"insert"}}}),
        TEST_VALUE("INSERT IGNORE INTO table1 (Usr, Count) VALUES ('allsp2', 3);", true,
                   {{"table1", {"insert"}}}),
        TEST_VALUE("INSERT INTO table1 (Usr, Count) VALUES ('allsp2', 3);SELECT * from table1",
                   true, {{"table1", {"insert", "select"}}}),
        TEST_VALUE("DELETE FROM table1 WHERE Count > 3;", true, {{"table1", {"delete"}}}),
        TEST_VALUE("DELETE LOW_PRIORITY FROM table1 WHERE Count > 3;", true,
                   {{"table1", {"delete"}}}),
        TEST_VALUE("DELETE QUICK FROM table1 WHERE Count > 3;", true, {{"table1", {"delete"}}}),
        TEST_VALUE("DELETE IGNORE FROM table1 WHERE Count > 3;", true, {{"table1", {"delete"}}}),

        TEST_VALUE("SELECT * FROM table1 WHERE Count = 1;", true, {{"table1", {"select"}}}),
        TEST_VALUE("SELECT * FROM table1 WHERE Count = 1;", true, {{"table1", {"select"}}}),
        TEST_VALUE("SELECT product.category FROM table1 WHERE Count = 1;", true,
                   {{"table1", {"select"}}, {"product", {"unknown"}}}),
        TEST_VALUE("SELECT DISTINCT Usr FROM table1;", true, {{"table1", {"select"}}}),
        TEST_VALUE("SELECT Usr, Count FROM table1 ORDER BY Count DESC;", true,
                   {{"table1", {"select"}}}),
        TEST_VALUE("SELECT 12 AS a, a FROM table1 GROUP BY a;", true, {{"table1", {"select"}}}),
        TEST_VALUE("SELECT;", false, {}), TEST_VALUE("SELECT Usr, Count FROM;", false, {}),
        TEST_VALUE("INSERT INTO table1 SELECT * FROM table2;", true,
                   {{"table1", {"insert"}}, {"table2", {"select"}}}),
        TEST_VALUE("INSERT INTO table1 SELECT tbl_temp1.fld_order_id FROM table2;", true,
                   {{"tbl_temp1", {"unknown"}}, {"table2", {"select"}}, {"table1", {"insert"}}}),
        TEST_VALUE("UPDATE table1 SET col1 = col1 + 1", true, {{"table1", {"update"}}}),
        TEST_VALUE("UPDATE LOW_PRIORITY table1 SET col1 = col1 + 1", true,
                   {{"table1", {"update"}}}),
        TEST_VALUE("UPDATE IGNORE table1 SET col1 = col1 + 1", true, {{"table1", {"update"}}}),
        TEST_VALUE("UPDATE table1 SET  column1=(SELECT * columnX from table2);", true,
                   {{"table1", {"update"}}, {"table2", {"select"}}}),

        // operations on database should not create any metadata
        TEST_VALUE("CREATE DATABASE testdb;", true, {}),
        TEST_VALUE("CREATE DATABASE IF NOT EXISTS testdb;", true, {}),
        TEST_VALUE("ALTER DATABASE testdb CHARACTER SET charset_name;", true, {}),
        TEST_VALUE("ALTER DATABASE testdb default CHARACTER SET charset_name;", true, {}),
        TEST_VALUE("ALTER DATABASE testdb default CHARACTER SET = charset_name;", true, {}),
        TEST_VALUE("ALTER SCHEMA testdb default CHARACTER SET = charset_name;", true, {}),

        // The following DROP DATABASE tests should not produce metadata.
        TEST_VALUE("DROP DATABASE testdb;", true, {}),
        TEST_VALUE("DROP DATABASE IF EXISTS testdb;", true, {}),

        // Schema. Should be parsed fine, but should not produce any metadata
        TEST_VALUE("SHOW databases;", true, {}), TEST_VALUE("SHOW tables;", true, {}),
        TEST_VALUE("SELECT * FROM;", false, {}),
        TEST_VALUE("SELECT 1 FROM tabletest1;", true, {{"tabletest1", {"select"}}})

            ));

} // namespace SQLUtils
} // namespace Common
} // namespace Extensions
} // namespace Envoy
