#include "source/common/buffer/buffer_impl.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec_command.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

TEST(MySQLCodecTest, MySQLCommandError) {
  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(""));
  Command mysql_cmd_decode{};
  decode_data->drain(4);
  mysql_cmd_decode.decode(*decode_data, 0, 0);
  EXPECT_EQ(mysql_cmd_decode.getCmd(), Command::Cmd::Null);
}

TEST(MySQLCodecTest, MySQLCommandInitDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::InitDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setDb(db);
  Buffer::OwnedImpl decode_data;
  mysql_cmd_encode.encode(decode_data);

  BufferHelper::encodeHdr(decode_data, 0);

  Command mysql_cmd_decode{};
  decode_data.drain(4);
  mysql_cmd_decode.decode(decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST(MySQLCodecTest, MySQLCommandCreateDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::CreateDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setDb(db);
  Buffer::OwnedImpl decode_data;
  mysql_cmd_encode.encode(decode_data);

  BufferHelper::encodeHdr(decode_data, 0);

  Command mysql_cmd_decode{};
  decode_data.drain(4);
  mysql_cmd_decode.decode(decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST(MySQLCodecTest, MySQLCommandDropDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::DropDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setDb(db);
  Buffer::OwnedImpl decode_data;
  mysql_cmd_encode.encode(decode_data);

  BufferHelper::encodeHdr(decode_data, 0);

  Command mysql_cmd_decode{};
  decode_data.drain(4);
  mysql_cmd_decode.decode(decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST(MySQLCodecTest, MySQLCommandOther) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::FieldList);
  Buffer::OwnedImpl decode_data;
  mysql_cmd_encode.encode(decode_data);
  BufferHelper::encodeHdr(decode_data, 0);

  Command mysql_cmd_decode{};
  decode_data.drain(4);
  mysql_cmd_decode.decode(decode_data, 0, 0);
  EXPECT_EQ(mysql_cmd_decode.getCmd(), Command::Cmd::FieldList);
}

TEST(MySQLCodecTest, MySQLCommandResp) {
  CommandResponse mysql_cmd_resp_encode{};
  mysql_cmd_resp_encode.setData(MySQLTestUtils::getCommandResponse());
  Buffer::OwnedImpl decode_data;
  mysql_cmd_resp_encode.encode(decode_data);
  CommandResponse mysql_cmd_resp_decode{};
  mysql_cmd_resp_decode.decode(decode_data, 0, decode_data.length());
  EXPECT_EQ(mysql_cmd_resp_decode.getData(), mysql_cmd_resp_encode.getData());
}

TEST(MySQLCodecTest, MySQLCommandRespIncompleteData) {
  CommandResponse mysql_cmd_resp_encode{};
  mysql_cmd_resp_encode.setData(MySQLTestUtils::getCommandResponse());
  Buffer::OwnedImpl decode_data;
  CommandResponse mysql_cmd_resp_decode{};
  mysql_cmd_resp_decode.decode(decode_data, 0, decode_data.length() + 1);
  EXPECT_EQ(mysql_cmd_resp_decode.getData(), "");
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
