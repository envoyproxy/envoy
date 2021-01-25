#include <bits/stdint-uintn.h>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLCodecTest : public testing::Test {};

TEST_F(MySQLCodecTest, MySQLCommandError) {
  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(""));
  Command mysql_cmd_decode{};
  decode_data->drain(4);
  mysql_cmd_decode.decode(*decode_data, 0, 0);
  EXPECT_EQ(mysql_cmd_decode.getCmd(), Command::Cmd::Null);
}

TEST_F(MySQLCodecTest, MySQLCommandInitDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::InitDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setData(db);
  Buffer::OwnedImpl decode_data;
  mysql_cmd_encode.encode(decode_data);

  BufferHelper::encodeHdr(decode_data, 0);

  Command mysql_cmd_decode{};
  decode_data.drain(4);
  mysql_cmd_decode.decode(decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST_F(MySQLCodecTest, MySQLCommandCreateDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::CreateDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setData(db);
  Buffer::OwnedImpl decode_data;
  mysql_cmd_encode.encode(decode_data);

  BufferHelper::encodeHdr(decode_data, 0);

  Command mysql_cmd_decode{};
  decode_data.drain(4);
  mysql_cmd_decode.decode(decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST_F(MySQLCodecTest, MySQLCommandDropDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::DropDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setData(db);
  Buffer::OwnedImpl decode_data;
  mysql_cmd_encode.encode(decode_data);

  BufferHelper::encodeHdr(decode_data, 0);

  Command mysql_cmd_decode{};
  decode_data.drain(4);
  mysql_cmd_decode.decode(decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST_F(MySQLCodecTest, MySQLCommandOther) {
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

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
