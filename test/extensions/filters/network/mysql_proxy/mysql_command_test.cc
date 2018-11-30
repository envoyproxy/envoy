#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "include/sqlparser/SQLParser.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLCommandTest : public MySQLTestUtils, public testing::Test {
public:
  int EncodeQuery(std::string query, hsql::SQLParserResult& result) {
    Command mysql_cmd_encode{};
    Command mysql_cmd_decode{};
    uint64_t offset = 0;
    int seq = 0;
    int len = 0;
    mysql_cmd_encode.setCmd(Command::Cmd::COM_QUERY);
    mysql_cmd_encode.setData(query);
    std::string data = mysql_cmd_encode.encode();
    std::string mysql_msg = BufferHelper::encodeHdr(data, 0);

    Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
    if (BufferHelper::peekHdr(*decode_data, offset, len, seq) != MYSQL_SUCCESS) {
      return MYSQL_FAILURE;
    }
    if (mysql_cmd_decode.decode(*decode_data, offset, seq, len) != MYSQL_SUCCESS) {
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

  std::string BuildShow(std::string resource) {
    std::string command("SHOW ");
    command.append(resource);
    return command;
  }

  std::string BuildUse(std::string db) {
    std::string command("USE ");
    command.append(db);
    return command;
  }

  // CREATE table
  std::string BuildCreate(enum TestResource res, std::string option, bool if_not_exists,
                          std::string res_name, std::string value) {
    std::string command("CREATE ");
    if (option != "") {
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
  std::string BuildAlter(enum TestResource res, std::string res_name, std::string values) {
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
  std::string BuildUpdate(std::string table, std::string option, std::string set_value) {
    std::string command("UPDATE ");
    command.append(option);
    command.append(SPACE);
    command.append(table);
    command.append(SPACE);
    command.append(set_value);
    return command;
  }

  // DROP Resource
  std::string BuildDrop(enum TestResource res, bool if_exists, std::string res_name) {
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
  std::string BuildInsert(std::string option, bool into, std::string table, std::string values) {
    std::string command("INSERT ");
    if (option != "") {
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
  std::string BuildDelete(std::string option, std::string table, std::string values) {
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
  std::string BuildSelect(std::string select_fields, std::string table, std::string where_clause) {
    std::string command("SELECT ");
    command.append(select_fields);
    command.append(SPACE);
    command.append(FROM);
    command.append(table);
    command.append(SPACE);
    command.append(where_clause);
    return command;
  }
};

int ReadStatement(hsql::SQLParserResult& result, std::string& exp_cmd, std::string& exp_table) {
  if (result.size() == 0) {
    return MYSQL_FAILURE;
  }
  for (auto i = 0u; i < result.size(); ++i) {
    hsql::TableAccessMap table_access_map;
    result.getStatement(i)->tablesAccessed(table_access_map);
    for (auto it = table_access_map.begin(); it != table_access_map.end(); ++it) {
      for (auto ot = it->second.begin(); ot != it->second.end(); ++ot) {
        exp_cmd = *ot;
        exp_table = it->first;
      }
    }
  }
  return MYSQL_SUCCESS;
}

/*
 * Tests query: "show databases"
 */
TEST_F(MySQLCommandTest, MySQLTest1) {
  std::string command = BuildShow("databases");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtShow);
}

/*
 * Tests query: "show tables"
 */
TEST_F(MySQLCommandTest, MySQLTest2) {
  std::string command = BuildShow("tables");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtShow);
}

/*
 * "CREATE table IF NOT EXISTS <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest3) {
  std::string table = "table1";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = BuildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, table);
}

/*
 * Tests query with optional cmd and quotes:
 * "CREATE table IF NOT EXISTS <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest4) {
  std::string table = "\"table1\"";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  hsql::SQLParserResult result;
  std::string command = BuildCreate(TestResource::TABLE, "", true, table, value);
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, "table1");
}

/*
 * Tests query with optional cmd and backticks:
 * "CREATE table IF NOT EXISTS <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest5) {
  std::string table = "`table1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = BuildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, "table1");
}

/*
 * Tests query with optional cmd:
 * "CREATE table IF NOT EXISTS <table_name_with_spaces>"
 */
TEST_F(MySQLCommandTest, MySQLTest6) {
  std::string table = "\"table 1\"";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  hsql::SQLParserResult result;
  std::string command = BuildCreate(TestResource::TABLE, "", true, table, value);
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, "table 1");
}

/*
 * Tests query with optional cmd:
 * "CREATE table IF NOT EXISTS <table_name_with_2_spaces>"
 */
TEST_F(MySQLCommandTest, MySQLTest7) {
  std::string table = "`table number 1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = BuildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, "table number 1");
}

/*
 * Test query with optional cmd:
 * "CREATE table IF NOT EXISTS <table_name_with_multi_spaces>"
 */
TEST_F(MySQLCommandTest, MySQLTest8) {
  std::string table = "`my sql table number 1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = BuildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, "my sql table number 1");
}

/*
 * Test query with optional cmd and backticks name delimiters
 * "CREATE table IF NOT EXISTS <table_name_with_multi_spaces_backticks>"
 */
TEST_F(MySQLCommandTest, MySQLTest9) {
  std::string table = "`my sql table number 1`";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = BuildCreate(TestResource::TABLE, "", true, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, "my sql table number 1");
}

/*
 * Test query: "CREATE table <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest10) {
  std::string table = "table1";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = BuildCreate(TestResource::TABLE, "", false, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, table);
}

/*
 * Negative Test query: "CREATE <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest11) {
  std::string table = "table1";
  std::string command = "CREATE ";
  command.append(table);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test query with optional cmd:
 * "CREATE TEMPORARY table <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest12) {
  std::string table = "table1";
  std::string value = "(Usr VARCHAR(40),Count INT);";
  std::string command = BuildCreate(TestResource::TABLE, "TEMPORARY", false, table, value);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "create");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query: "CREATE DATABASE <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest13) {
  std::string db = "mysqldb";
  std::string command = BuildCreate(TestResource::DB, "", false, db, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
}

/*
 * Test query with optional cmd:
 * "CREATE DATABASE IF NOT EXISTS <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest14) {
  std::string db = "mysqldb";
  std::string command = BuildCreate(TestResource::DB, "", true, db, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtCreate);
}

/*
 * Test query: "CREATE EVENT <event>"
 */
TEST_F(MySQLCommandTest, MySQLTest15) {
  std::string event = "event1";
  std::string command = BuildCreate(TestResource::EVENT, "", false, event, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test query: "ALTER DATABASE <DB> CHARACTER SET charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest16) {
  std::string db = "mysqldb";
  std::string command = BuildAlter(TestResource::DB, db, "CHARACTER SET charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size()); 
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtAlter);
}

/*
 * Test query: "ALTER DATABASE <DB> default CHARACTER SET charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest17) {
  std::string db = "mysqldb";
  std::string command = BuildAlter(TestResource::DB, db, "default CHARACTER SET charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtAlter);
}

/*
 * Test query: "ALTER DATABASE <DB> default CHARACTER SET = charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest18) {
  std::string db = "mysqldb";
  std::string command = BuildAlter(TestResource::DB, db, "default CHARACTER SET = charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtAlter);
}

/*
 * Test query: "ALTER SCHEMA <DB> default CHARACTER SET = charset_name"
 */
TEST_F(MySQLCommandTest, MySQLTest19) {
  std::string db = "mysqldb";
  std::string command = BuildAlter(TestResource::SCHEMA, db, "default CHARACTER SET = charset_name");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtAlter);
}

/*
 * Test query: "ALTER TABLE <table> add column Id varchar (20)"
 */
TEST_F(MySQLCommandTest, MySQLTest20) {
  std::string table = "table1";
  std::string command = BuildAlter(TestResource::TABLE, table, "add column Id varchar (20)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtAlter);
}

/*
 * Test query: "DROP DATABASE <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest21) {
  std::string db = "mysqldb";
  std::string command = BuildDrop(TestResource::DB, false, db);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtDrop);
}

/*
 * Test query with optional cmd:
 * "DROP DATABASE IF EXISTS <DB>"
 */
TEST_F(MySQLCommandTest, MySQLTest22) {
  std::string db = "mysqldb";
  std::string command = BuildDrop(TestResource::DB, true, db);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtDrop);
}

/*
 * Test query with optional cmd:
 * "DROP TABLE IF EXISTS <Table>"
 */
TEST_F(MySQLCommandTest, MySQLTest23) {
  std::string table = "table1";
  std::string command = BuildDrop(TestResource::TABLE, true, table);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtDrop);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "drop");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query INSERT:
 * "INSERT INTO <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest24) {
  std::string table = "table1";
  std::string command = BuildInsert("", true, table, " (Usr, Count) VALUES ('allsp2', 3)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtInsert);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "insert");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query INSERT with optional parameters:
 * "INSERT LOW_PRIORITY INTO <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest25) {
  std::string table = "table1";
  std::string command =
      BuildInsert("LOW_PRIORITY", true, table, " (Usr, Count) VALUES ('allsp2', 3)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtInsert);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "insert");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query INSERT with optional parameters:
 * "INSERT IGNORE INTO <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest26) {
  std::string table = "table1";
  std::string command = BuildInsert("IGNORE", true, table, " (Usr, Count) VALUES ('allsp2', 3)");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtInsert);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "insert");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query DELETE:
 * "DELETE FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest27) {
  std::string table = "table1";
  std::string command = BuildDelete("", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtDelete);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "delete");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query DELETE with optional parameters:
 * "DELETE LOW_PRIORITY FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest28) {
  std::string table = "table1";
  std::string command = BuildDelete("LOW_PRIORITY", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtDelete);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "delete");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query DELETE with optional parameters:
 * "DELETE QUICK FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest29) {
  std::string table = "table1";
  std::string command = BuildDelete("QUICK", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtDelete);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "delete");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query DELETE with optional parameters:
 * "DELETE QUICK FROM <table> (Usr, Count) VALUES ('allsp2', 3)"
 */
TEST_F(MySQLCommandTest, MySQLTest30) {
  std::string table = "table1";
  std::string command = BuildDelete("IGNORE", table, "WHERE Count > 3");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtDelete);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "delete");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query SELECT:
 * "SELECT * FROM <table> ProductDetails WHERE Count = 1"
 */
TEST_F(MySQLCommandTest, MySQLTest31) {
  std::string table = "table1";
  std::string command = BuildSelect("*", table, "WHERE Count = 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtSelect);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "select");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query SELECT:
 * "SELECT FROM <table> ProductDetails WHERE Count = 1"
 */
TEST_F(MySQLCommandTest, MySQLTest32) {
  std::string table = "table1";
  std::string command = BuildSelect("Product.category", table, "WHERE Count = 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtSelect);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "select");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query SELECT:
 * "SELECT DISTINCT Usr FROM <table>"
 */
TEST_F(MySQLCommandTest, MySQLTest33) {
  std::string table = "table1";
  std::string command = BuildSelect("DISTINCT Usr", table, "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtSelect);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "select");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query SELECT:
 * "SELECT Usr,Count FROM <table> ORDER BY Count DESC"
 */
TEST_F(MySQLCommandTest, MySQLTest34) {
  std::string table = "table1";
  std::string command = BuildSelect("Usr,Count", table, "ORDER BY Count DESC");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtSelect);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "select");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query SELECT:
 * "SELECT Usr,Count FROM <table> ORDER BY Count DESC"
 */
TEST_F(MySQLCommandTest, MySQLTest35) {
  std::string table = "table1";
  std::string command = BuildSelect("Usr,Count", table, "ORDER BY Count DESC");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtSelect);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "select");
  EXPECT_EQ(exp_table, table);
}

/*
 * Negative Test query: SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest36) {
  std::string command = BuildSelect("", "", "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test query: SELECT no FROM
 */
TEST_F(MySQLCommandTest, MySQLTest37) {
  std::string command = BuildSelect("USr,Count", "", "");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(false, result.isValid());
}

/*
 * Test correlated queries: INSERT, SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest38) {
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string ins_command = BuildInsert("", true, table1, "");
  std::string sel_command =
      BuildSelect("*", table2, "" /*"WHERE tbl_temp1.fld_order_id > 100"*/);
  ins_command.append(sel_command);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(ins_command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtInsert);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "insert");
  EXPECT_EQ(exp_table, table1);
}

/*
 * Test not correlated queries: INSERT, SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest39) {
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string ins_command = BuildInsert("", true, table1, "");
  std::string sel_command =
      BuildSelect("tbl_temp1.fld_order_id", table2, "");
  ins_command.append(sel_command);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(ins_command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtInsert);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "insert");
  EXPECT_EQ(exp_table, table1);
}

/*
 * Negative Test query: INSERT, Wrong SELECR
 */
TEST_F(MySQLCommandTest, MySQLTest40) {
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string ins_command = "INSERT INTO ";
  std::string ins_command2 = " (fld_id) ";
  std::string sel_command =
      BuildSelect("tbl_temp1.fld_order_id", table1, "WHERE tbl_temp1.fld_order_id > 100;");
  ins_command.append(table1);
  ins_command.append(ins_command2);
  ins_command.append(sel_command);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(ins_command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest41) {
  std::string table = "table1";
  std::string command = BuildUpdate(table, "", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtUpdate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "update");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest42) {
  std::string table = "table1";
  std::string command = BuildUpdate(table, "LOW_PRIORITY", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtUpdate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "update");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest43) {
  std::string table = "table1";
  std::string command = BuildUpdate(table, "IGNORE", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtUpdate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "update");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test query: UPDATE
 */
TEST_F(MySQLCommandTest, MySQLTest44) {
  std::string table = "table1";
  std::string command = BuildUpdate(table, "LOW_PRIORITY IGNORE", "SET col1 = col1 + 1");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtUpdate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "update");
  EXPECT_EQ(exp_table, table);
}

/*
 * Test correlated queries: UPDATE, SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest45) {
  std::string table1 = "table1";
  std::string table2 = "table2";
  std::string command = BuildUpdate(table1, "", "set column1=");
  std::string command2 = BuildSelect("columnX", table2, ")");
  command.append("(");
  command.append(command2);
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtUpdate);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "select");
  EXPECT_EQ(exp_table, table2);
}

/*
 * Test query: SELECT
 */
TEST_F(MySQLCommandTest, MySQLTest46) {
  std::string table = "table1";
  std::string command = BuildSelect("12 AS a, a ", table, "GROUP BY a;");
  hsql::SQLParserResult result;
  EXPECT_EQ(MYSQL_SUCCESS, EncodeQuery(command, result));
  EXPECT_EQ(true, result.isValid());
  EXPECT_EQ(1UL, result.size());
  EXPECT_EQ(result.getStatement(0)->type(), hsql::StatementType::kStmtSelect);
  std::string exp_cmd;
  std::string exp_table;
  EXPECT_EQ(MYSQL_SUCCESS, ReadStatement(result, exp_cmd, exp_table));
  EXPECT_EQ(exp_cmd, "select");
  EXPECT_EQ(exp_table, table);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
