#include <cstdint>

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "include/sqlparser/SQLParser.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLCommandTest : public testing::Test, public MySQLTestUtils {
public:
  int encodeQuery(std::string query, hsql::SQLParserResult& result) {
    Command mysql_cmd_encode{};
    Command mysql_cmd_decode{};
    uint8_t seq = 0u;
    uint32_t len = 0u;
    mysql_cmd_encode.setCmd(Command::Cmd::Query);
    mysql_cmd_encode.setData(query);
    std::string data = mysql_cmd_encode.encode();
    std::string mysql_msg = BufferHelper::encodeHdr(data, 0);

    Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
    if (BufferHelper::peekHdr(*decode_data, len, seq) != MYSQL_SUCCESS) {
      return MYSQL_FAILURE;
    }
    BufferHelper::consumeHdr(*decode_data);
    if (mysql_cmd_decode.decode(*decode_data, seq, len) != MYSQL_SUCCESS) {
      return MYSQL_FAILURE;
    }
    hsql::SQLParser::parse(mysql_cmd_decode.getData(), &result);
    return MYSQL_SUCCESS;
  }

  enum TestResource {
    TABLE,
    DB,
    SCHEMA,
    EVENT,
    INDEX,
  };

  const std::string SPACE = " ";
  const std::string FROM = "FROM ";
  const std::string INTO = "INTO ";
  const std::string IF_EXISTS = "IF EXISTS ";
  const std::string IF_NOT_EXISTS = "IF NOT EXISTS ";

  std::string buildShow(std::string resource) {
    std::string command("SHOW ");
    command.append(resource);
    return command;
  }

  std::string buildUse(std::string db) {
    std::string command("USE ");
    command.append(db);
    return command;
  }

  // CREATE table
  std::string buildCreate(enum TestResource res, std::string option, bool if_not_exists,
                          std::string res_name, std::string value) {
    std::string command("CREATE ");
    if (!option.empty()) {
      command.append(option);
      command.append(SPACE);
    }
    switch (res) {
    case TABLE:
      command.append("TABLE ");
      break;
    case DB:
      command.append("DATABASE ");
      break;
    case EVENT:
      command.append("EVENT ");
      break;
    case INDEX:
      command.append("INDEX ");
      break;
    default:
      return command;
    }
    if (if_not_exists) {
      command.append(IF_NOT_EXISTS);
    }
    command.append(res_name);
    command.append(SPACE);
    command.append(value);
    return command;
  }

  // ALTER a resource
  std::string buildAlter(enum TestResource res, std::string res_name, std::string values) {
    std::string command("ALTER ");
    switch (res) {
    case TABLE:
      command.append("TABLE ");
      break;
    case DB:
      command.append("DATABASE ");
      break;
    case SCHEMA:
      command.append("SCHEMA ");
      break;
    default:
      return command;
    }
    command.append(res_name);
    command.append(SPACE);
    command.append(values);
    return command;
  }

  // UPDATE
  std::string buildUpdate(std::string table, std::string option, std::string set_value) {
    std::string command("UPDATE ");
    command.append(option);
    command.append(SPACE);
    command.append(table);
    command.append(SPACE);
    command.append(set_value);
    return command;
  }

  // DROP Resource
  std::string buildDrop(enum TestResource res, bool if_exists, std::string res_name) {
    std::string command("DROP ");
    switch (res) {
    case TABLE:
      command.append("TABLE ");
      break;
    case DB:
      command.append("DATABASE ");
      break;
    case EVENT:
      command.append("SCHEMA ");
      break;
    default:
      return command;
    }
    if (if_exists) {
      command.append(IF_EXISTS);
    }
    command.append(res_name);
    return command;
  }

  //"INSERT INTO <table> ...
  std::string buildInsert(std::string option, bool into, std::string table, std::string values) {
    std::string command("INSERT ");
    if (!option.empty()) {
      command.append(option);
      command.append(SPACE);
    }
    if (into) {
      command.append(INTO);
    }
    command.append(table);
    command.append(SPACE);
    command.append(values);
    return command;
  }

  // DELETE FROM <table> ...
  std::string buildDelete(std::string option, std::string table, std::string values) {
    std::string command("DELETE ");
    command.append(option);
    command.append(SPACE);
    command.append(FROM);
    command.append(table);
    command.append(SPACE);
    command.append(values);
    return command;
  }

  // SELECT FROM <table> ...
  std::string buildSelect(std::string select_fields, std::string table, std::string where_clause) {
    std::string command("SELECT ");
    command.append(select_fields);
    command.append(SPACE);
    command.append(FROM);
    command.append(table);
    command.append(SPACE);
    command.append(where_clause);
    return command;
  }

  void expectStatementTypeAndTableAccessMap(const hsql::SQLParserResult& result,
                                            hsql::StatementType statement_type,
                                            const hsql::TableAccessMap& expected_table_access_map) {
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(1UL, result.size());
    EXPECT_EQ(statement_type, result.getStatement(0)->type());
    hsql::TableAccessMap table_access_map;
    if (expected_table_access_map.empty() && (statement_type == hsql::StatementType::kStmtShow)) {
      return;
    }
    result.getStatement(0)->tablesAccessed(table_access_map);
    EXPECT_EQ(table_access_map, expected_table_access_map);
  }
};

/*
 * Tests query: "show databases"
 */
TEST_F(MySQLCommandTest, MySQLTest1) {
  std::string command = buildShow("databases");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtShow, {});
}

/*
 * Tests query: "show tables"
 */
TEST_F(MySQLCommandTest, MySQLTest2) {
  std::string command = buildShow("tables");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtShow, {});
}

/*
 * "CREATE table IF NOT EXISTS <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest3) {
  std::string table = "table1";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = buildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{table, {"create"}}});
}

/*
 * Tests query with optional cmd and quotes:
 * "CREATE table IF NOT EXISTS <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest4) {
  std::string table = "\"table1\"";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  hsql::SQLParserResult result;
  std::string command = buildCreate(TestResource::TABLE, "", true, table, value);
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{"table1", {"create"}}});
}

/*
 * Tests query with optional cmd and backticks:
 * "CREATE table IF NOT EXISTS <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest5) {
  std::string table = "`table1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = buildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{"table1", {"create"}}});
}

/*
 * Tests query with optional cmd:
 * "CREATE table IF NOT EXISTS <table_name_with_spaces>"
 */
TEST_F(MySQLCommandTest, MySQLTest6) {
  std::string table = "\"table 1\"";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  hsql::SQLParserResult result;
  std::string command = buildCreate(TestResource::TABLE, "", true, table, value);
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{"table 1", {"create"}}});
}

/*
 * Tests query with optional cmd:
 * "CREATE table IF NOT EXISTS <table_name_with_2_spaces>"
 */
TEST_F(MySQLCommandTest, MySQLTest7) {
  std::string table = "`table number 1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = buildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{"table number 1", {"create"}}});
}

/*
 * Test query with optional cmd:
 * "CREATE table IF NOT EXISTS <table_name_with_multi_spaces>"
 */
TEST_F(MySQLCommandTest, MySQLTest8) {
  std::string table = "`my sql table number 1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = buildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{"my sql table number 1", {"create"}}});
}

/*
 * Test query with optional cmd and backticks name delimiters
 * "CREATE table IF NOT EXISTS <table_name_with_multi_spaces_backticks>"
 */
TEST_F(MySQLCommandTest, MySQLTest9) {
  std::string table = "`my sql table number 1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = buildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{"my sql table number 1", {"create"}}});
}

/*
 * Test query: "CREATE table <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest10) {
  std::string table = "table1";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = buildCreate(TestResource::TABLE, "", false, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{table, {"create"}}});
}

/*
 * Negative Test query: "CREATE <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest11) {
  std::string table = "table1";
  std::string command = "CREATE ";
  command.append(table);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test query with optional cmd:
 * "CREATE TEMPORARY table <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest12) {
  std::string table = "table1";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = buildCreate(TestResource::TABLE, "TEMPORARY", false, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate,
                                       {{"table1", {"create"}}});
}

/*
 * Test query: "CREATE DATABASE <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest13) {
  std::string db = "mysqldb";
  std::string command = buildCreate(TestResource::DB, "", false, db, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate, {});
}

/*
 * Test query with optional cmd:
 * "CREATE DATABASE IF NOT EXISTS <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest14) {
  std::string db = "mysqldb";
  std::string command = buildCreate(TestResource::DB, "", true, db, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtCreate, {});
}

/*
 * Test query: "CREATE EVENT <event>"
 */
TEST_F(MySQLCommandTest, MySQLTest15) {
  std::string event = "event1";
  std::string command = buildCreate(TestResource::EVENT, "", false, event, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test query: "ALTER DATABASE <DB> CHARACTER SET charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest16) {
  std::string db = "mysqldb";
  std::string command = buildAlter(TestResource::DB, db, "CHARACTER SET charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtAlter, {});
}

/*
 * Test query: "ALTER DATABASE <DB> default CHARACTER SET charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest17) {
  std::string db = "mysqldb";
  std::string command = buildAlter(TestResource::DB, db, "default CHARACTER SET charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtAlter, {});
}

/*
 * Test query: "ALTER DATABASE <DB> default CHARACTER SET = charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest18) {
  std::string db = "mysqldb";
  std::string command = buildAlter(TestResource::DB, db, "default CHARACTER SET = charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtAlter, {});
}

/*
 * Test query: "ALTER SCHEMA <DB> default CHARACTER SET = charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest19) {
  std::string db = "mysqldb";
  std::string command =
      buildAlter(TestResource::SCHEMA, db, "default CHARACTER SET = charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtAlter, {});
}

/*
 * Test query: "ALTER TABLE <table> add column Id varchar (20)"
 */
TEST_F(MySQLCommandTest, MySQLTest20) {
  std::string table = "table1";
  std::string command = buildAlter(TestResource::TABLE, table, "add column Id varchar (20)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtAlter,
                                       {{table, {"alter"}}});
}

/*
 * Test query: "DROP DATABASE <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest21) {
  std::string db = "mysqldb";
  std::string command = buildDrop(TestResource::DB, false, db);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtDrop, {});
}

/*
 * Test query with optional cmd:
 * "DROP DATABASE IF EXISTS <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest22) {
  std::string db = "mysqldb";
  std::string command = buildDrop(TestResource::DB, true, db);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtDrop, {});
}

/*
 * Test query with optional cmd:
 * "DROP TABLE IF EXISTS <Table>"
 */
TEST_F(MySQLCommandTest, MySQLTest23) {
  std::string table = "table1";
  std::string command = buildDrop(TestResource::TABLE, true, table);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtDrop, {{table, {"drop"}}});
}

/*
 * Test query INSERT:
 * "INSERT INTO <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest24) {
  std::string table = "table1";
  std::string command = buildInsert("", true, table, " (Usr, Count) VALUES ('allsp2', 3)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtInsert,
                                       {{table, {"insert"}}});
}

/*
 * Test query INSERT with optional parameters:
 * "INSERT LOW_PRIORITY INTO <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest25) {
  std::string table = "table1";
  std::string command =
      buildInsert("LOW_PRIORITY", true, table, " (Usr, Count) VALUES ('allsp2', 3)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtInsert,
                                       {{table, {"insert"}}});
}

/*
 * Test query INSERT with optional parameters:
 * "INSERT IGNORE INTO <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest26) {
  std::string table = "table1";
  std::string command = buildInsert("IGNORE", true, table, " (Usr, Count) VALUES ('allsp2', 3)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtInsert,
                                       {{table, {"insert"}}});
}

/*
 * Test query DELETE:
 * "DELETE FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest27) {
  std::string table = "table1";
  std::string command = buildDelete("", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtDelete,
                                       {{table, {"delete"}}});
}

/*
 * Test query DELETE with optional parameters:
 * "DELETE LOW_PRIORITY FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest28) {
  std::string table = "table1";
  std::string command = buildDelete("LOW_PRIORITY", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtDelete,
                                       {{table, {"delete"}}});
}

/*
 * Test query DELETE with optional parameters:
 * "DELETE QUICK FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest29) {
  std::string table = "table1";
  std::string command = buildDelete("QUICK", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtDelete,
                                       {{table, {"delete"}}});
}

/*
 * Test query DELETE with optional parameters:
 * "DELETE QUICK FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest30) {
  std::string table = "table1";
  std::string command = buildDelete("IGNORE", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtDelete,
                                       {{table, {"delete"}}});
}

/*
 * Test query SELECT:
 * "SELECT * FROM <table> ProductDetails WHERE Count = 1"
 */
TEST_F(MySQLCommandTest, MySQLTest31) {
  std::string table = "table1";
  std::string command = buildSelect("*", table, "WHERE Count = 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtSelect,
                                       {{table, {"select"}}});
}

/*
 * Test query SELECT:
 * "SELECT FROM <table> ProductDetails WHERE Count = 1"
 */
TEST_F(MySQLCommandTest, MySQLTest32) {
  std::string table = "table1";
  std::string command = buildSelect("Product.category", table, "WHERE Count = 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtSelect,
                                       {{table, {"select"}}, {"Product", {"unknown"}}});
}

/*
 * Test query SELECT:
 * "SELECT DISTINCT Usr FROM <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest33) {
  std::string table = "table1";
  std::string command = buildSelect("DISTINCT Usr", table, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtSelect,
                                       {{table, {"select"}}});
}

/*
 * Test query SELECT:
 * "SELECT Usr,Count FROM <table> ORDER BY Count DESC"
 */
TEST_F(MySQLCommandTest, MySQLTest34) {
  std::string table = "table1";
  std::string command = buildSelect("Usr,Count", table, "ORDER BY Count DESC");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtSelect,
                                       {{table, {"select"}}});
}

/*
 * Test query SELECT:
 * "SELECT Usr,Count FROM <table> ORDER BY Count DESC"
 */
TEST_F(MySQLCommandTest, MySQLTest35) {
  std::string table = "table1";
  std::string command = buildSelect("Usr,Count", table, "ORDER BY Count DESC");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtSelect,
                                       {{table, {"select"}}});
}

/*
 * Negative Test query: SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest36) {
  std::string command = buildSelect("", "", "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test query: SELECT no FROM
 */
TEST_F(MySQLCommandTest, MySQLTest37) {
  std::string command = buildSelect("USr,Count", "", "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test correlated queries: INSERT, SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest38) {
  // SPELLCHECKER(off)
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string ins_command = buildInsert("", true, table1, "");
  std::string sel_command = buildSelect("*", table2, "" /*"WHERE tbl_temp1.fld_order_id > 100"*/);
  ins_command.append(sel_command);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(ins_command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtInsert,
                                       {{table1, {"insert"}}, {table2, {"select"}}});
  // SPELLCHECKER(on)
}

/*
 * Test not correlated queries: INSERT, SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest39) {
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string ins_command = buildInsert("", true, table1, "");
  std::string sel_command = buildSelect("tbl_temp1.fld_order_id", table2, "");
  ins_command.append(sel_command);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(ins_command, result));
  expectStatementTypeAndTableAccessMap(
      result, hsql::StatementType::kStmtInsert,
      {{"tbl_temp1", {"unknown"}}, {"table2", {"select"}}, {"table1", {"insert"}}});
}

/*
 * Negative Test query: INSERT, Wrong SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest40) {
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string ins_command = "INSERT INTO ";
  std::string ins_command2 = " (fld_id) ";
  std::string sel_command =
      buildSelect("tbl_temp1.fld_order_id", table1, "WHERE tbl_temp1.fld_order_id > 100;");
  ins_command.append(table1);
  ins_command.append(ins_command2);
  ins_command.append(sel_command);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(ins_command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest41) {
  std::string table = "table1";
  std::string command = buildUpdate(table, "", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtUpdate,
                                       {{table, {"update"}}});
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest42) {
  std::string table = "table1";
  std::string command = buildUpdate(table, "LOW_PRIORITY", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtUpdate,
                                       {{table, {"update"}}});
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest43) {
  std::string table = "table1";
  std::string command = buildUpdate(table, "IGNORE", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtUpdate,
                                       {{table, {"update"}}});
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest44) {
  std::string table = "table1";
  std::string command = buildUpdate(table, "LOW_PRIORITY IGNORE", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtUpdate,
                                       {{table, {"update"}}});
}

/*
 * Test correlated queries: UPDATE, SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest45) {
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string command = buildUpdate(table1, "", "set column1=");
  std::string command2 = buildSelect("columnX", table2, ")");
  command.append("(");
  command.append(command2);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtUpdate,
                                       {{table1, {"update"}}, {table2, {"select"}}});
}

/*
 * Test query: SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest46) {
  std::string table = "table1";
  std::string command = buildSelect("12 AS a, a ", table, "GROUP BY a;");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, encodeQuery(command, result));
  expectStatementTypeAndTableAccessMap(result, hsql::StatementType::kStmtSelect,
                                       {{table, {"select"}}});
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
