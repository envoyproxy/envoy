#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"

#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int MYSQL_UT_LAST_ID = 0;
constexpr int MYSQL_UT_SERVER_OK = 0;
constexpr int MYSQL_UT_SERVER_WARNINGS = 0x0001;

/*
 * Test the MYSQL Server Login Response OK message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST(MySQLCLoginRespTest, MySQLLoginOkEncDec) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.type(Ok);
  mysql_loginok_encode.asOkMessage().setAffectedRows(1);
  mysql_loginok_encode.asOkMessage().setLastInsertId(MYSQL_UT_LAST_ID);
  mysql_loginok_encode.asOkMessage().setServerStatus(MYSQL_UT_SERVER_OK);
  mysql_loginok_encode.asOkMessage().setWarnings(MYSQL_UT_SERVER_WARNINGS);
  Buffer::OwnedImpl decode_data;
  mysql_loginok_encode.encode(decode_data);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode, mysql_loginok_encode);
}

/*
 * Test the MYSQL Server Login Response Err message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST(MySQLCLoginRespTest, MySQLLoginErrEncDec) {
  ClientLoginResponse mysql_loginerr_encode{};
  mysql_loginerr_encode.type(Err);
  mysql_loginerr_encode.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
  mysql_loginerr_encode.asErrMessage().setSqlStateMarker('#');
  mysql_loginerr_encode.asErrMessage().setSqlState(MySQLTestUtils::getSqlState());
  mysql_loginerr_encode.asErrMessage().setErrorMessage(MySQLTestUtils::getErrorMessage());
  Buffer::OwnedImpl decode_data;
  mysql_loginerr_encode.encode(decode_data);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode, mysql_loginerr_encode);
}

/*
 * Test the MYSQL Server Login Old Auth Switch message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST(MySQLCLoginRespTest, MySQLLoginOldAuthSwitch) {
  ClientLoginResponse mysql_old_auth_switch_encode{};
  mysql_old_auth_switch_encode.type(AuthSwitch);
  Buffer::OwnedImpl decode_data;
  mysql_old_auth_switch_encode.encode(decode_data);

  ClientLoginResponse mysql_old_auth_switch_decode{};
  mysql_old_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_old_auth_switch_decode, mysql_old_auth_switch_encode);
}

/*
 * Test the MYSQL Server Login Auth Switch message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST(MySQLCLoginRespTest, MySQLLoginAuthSwitch) {
  ClientLoginResponse mysql_auth_switch_encode{};
  mysql_auth_switch_encode.type(AuthSwitch);
  mysql_auth_switch_encode.asAuthSwitchMessage().setAuthPluginName(
      MySQLTestUtils::getAuthPluginName());
  mysql_auth_switch_encode.asAuthSwitchMessage().setAuthPluginData(
      MySQLTestUtils::getAuthPluginData20());
  Buffer::OwnedImpl decode_data;
  mysql_auth_switch_encode.encode(decode_data);

  ClientLoginResponse mysql_auth_switch_decode{};
  mysql_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_auth_switch_decode, mysql_auth_switch_encode);
}

/*
 * Test the MYSQL Server Login Auth More message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST(MySQLCLoginRespTest, MySQLLoginAuthMore) {
  ClientLoginResponse mysql_auth_more_encode{};
  mysql_auth_more_encode.type(AuthMoreData);
  mysql_auth_more_encode.asAuthMoreMessage().setAuthMoreData(MySQLTestUtils::getAuthPluginData20());
  Buffer::OwnedImpl decode_data;
  mysql_auth_more_encode.encode(decode_data);

  ClientLoginResponse mysql_auth_more_decode{};
  mysql_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_auth_more_decode, mysql_auth_more_encode);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete response code
 */
TEST(MySQLCLoginRespTest, MySQLLoginOkIncompleteRespCode) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.type(Ok);
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete affected rows
 */
TEST(MySQLCLoginRespTest, MySQLLoginOkIncompleteAffectedRows) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.type(Ok);
  mysql_loginok_encode.asOkMessage().setAffectedRows(1);
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.type(), mysql_loginok_encode.type());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK last insert id
 */
TEST(MySQLCLoginRespTest, MySQLLoginOkIncompleteLastInsertId) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.type(Ok);
  mysql_loginok_encode.asOkMessage().setAffectedRows(1);
  mysql_loginok_encode.asOkMessage().setLastInsertId(MYSQL_UT_LAST_ID);
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) + MySQLTestUtils::sizeOfLengthEncodeInteger(
                                             mysql_loginok_encode.asOkMessage().getAffectedRows());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.type(), mysql_loginok_encode.type());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getAffectedRows(),
            mysql_loginok_encode.asOkMessage().getAffectedRows());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete server status
 */
TEST(MySQLCLoginRespTest, MySQLLoginOkIncompleteServerStatus) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.type(Ok);
  mysql_loginok_encode.asOkMessage().setAffectedRows(1);
  mysql_loginok_encode.asOkMessage().setLastInsertId(MYSQL_UT_LAST_ID);
  mysql_loginok_encode.asOkMessage().setServerStatus(MYSQL_UT_SERVER_OK);
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) +
                       MySQLTestUtils::sizeOfLengthEncodeInteger(
                           mysql_loginok_encode.asOkMessage().getAffectedRows()) +
                       MySQLTestUtils::sizeOfLengthEncodeInteger(
                           mysql_loginok_encode.asOkMessage().getLastInsertId());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.type(), mysql_loginok_encode.type());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getAffectedRows(),
            mysql_loginok_encode.asOkMessage().getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getLastInsertId(),
            mysql_loginok_encode.asOkMessage().getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getServerStatus(), 0);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete warnings
 */
TEST(MySQLCLoginRespTest, MySQLLoginOkIncompleteWarnings) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.type(Ok);
  mysql_loginok_encode.asOkMessage().setAffectedRows(1);
  mysql_loginok_encode.asOkMessage().setLastInsertId(MYSQL_UT_LAST_ID);
  mysql_loginok_encode.asOkMessage().setServerStatus(MYSQL_UT_SERVER_OK);
  mysql_loginok_encode.asOkMessage().setWarnings(MYSQL_UT_SERVER_WARNINGS);
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) +
                       MySQLTestUtils::sizeOfLengthEncodeInteger(
                           mysql_loginok_encode.asOkMessage().getAffectedRows()) +
                       MySQLTestUtils::sizeOfLengthEncodeInteger(
                           mysql_loginok_encode.asOkMessage().getLastInsertId()) +
                       sizeof(mysql_loginok_encode.asOkMessage().getServerStatus());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.type(), mysql_loginok_encode.type());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getAffectedRows(),
            mysql_loginok_encode.asOkMessage().getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getLastInsertId(),
            mysql_loginok_encode.asOkMessage().getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getServerStatus(),
            mysql_loginok_encode.asOkMessage().getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getWarnings(), 0);
}

/*
 * Negative Test the MYSQL Server Login Err message parser:
 * - incomplete response code
 */
TEST(MySQLCLoginRespTest, MySQLLoginErrIncompleteRespCode) {
  ClientLoginResponse mysql_loginerr_encode{};
  mysql_loginerr_encode.type(Err);
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete error code
 */
TEST(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorcode) {
  ClientLoginResponse mysql_loginerr_encode{};
  mysql_loginerr_encode.type(Err);
  mysql_loginerr_encode.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.type(), mysql_loginerr_encode.type());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorCode(), 0);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete sql state marker
 */
TEST(MySQLCLoginRespTest, MySQLLoginErrIncompleteStateMarker) {
  ClientLoginResponse mysql_loginerr_encode{};
  mysql_loginerr_encode.type(Err);
  mysql_loginerr_encode.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
  mysql_loginerr_encode.asErrMessage().setSqlStateMarker('#');
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) + sizeof(mysql_loginerr_encode.asErrMessage().getErrorCode());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.type(), mysql_loginerr_encode.type());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorCode(),
            mysql_loginerr_encode.asErrMessage().getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlStateMarker(), 0);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete sql state
 */
TEST(MySQLCLoginRespTest, MySQLLoginErrIncompleteSqlState) {
  ClientLoginResponse mysql_loginerr_encode{};
  mysql_loginerr_encode.type(Err);
  mysql_loginerr_encode.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
  mysql_loginerr_encode.asErrMessage().setSqlStateMarker('#');
  mysql_loginerr_encode.asErrMessage().setSqlState(MySQLTestUtils::getSqlState());
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) +
                       sizeof(mysql_loginerr_encode.asErrMessage().getErrorCode()) +
                       sizeof(mysql_loginerr_encode.asErrMessage().getSqlStateMarker());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.type(), mysql_loginerr_encode.type());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorCode(),
            mysql_loginerr_encode.asErrMessage().getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlStateMarker(),
            mysql_loginerr_encode.asErrMessage().getSqlStateMarker());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlState(), "");
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete error message
 */
TEST(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorMessage) {
  ClientLoginResponse mysql_loginerr_encode{};
  mysql_loginerr_encode.type(Err);
  mysql_loginerr_encode.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
  mysql_loginerr_encode.asErrMessage().setSqlStateMarker('#');
  mysql_loginerr_encode.asErrMessage().setSqlState(MySQLTestUtils::getSqlState());
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.asErrMessage().setErrorMessage(MySQLTestUtils::getErrorMessage());
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) +
                       sizeof(mysql_loginerr_encode.asErrMessage().getErrorCode()) +
                       sizeof(mysql_loginerr_encode.asErrMessage().getSqlStateMarker()) +
                       mysql_loginerr_encode.asErrMessage().getSqlState().size();
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.type(), mysql_loginerr_encode.type());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorCode(),
            mysql_loginerr_encode.asErrMessage().getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlStateMarker(),
            mysql_loginerr_encode.asErrMessage().getSqlStateMarker());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlState(),
            mysql_loginerr_encode.asErrMessage().getSqlState());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorMessage(), "");
}

/*
 * Negative Test the MYSQL Server Login Auth Switch message parser:
 * - incomplete response code
 */
TEST(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompleteRespCode) {
  ClientLoginResponse mysql_login_auth_switch_encode{};
  mysql_login_auth_switch_encode.type(AuthSwitch);
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete auth plugin name
 */
TEST(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginName) {
  ClientLoginResponse mysql_login_auth_switch_encode{};
  mysql_login_auth_switch_encode.type(AuthSwitch);
  mysql_login_auth_switch_encode.asAuthSwitchMessage().setAuthPluginName(
      MySQLTestUtils::getAuthPluginName());
  Buffer::OwnedImpl buffer;
  mysql_login_auth_switch_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.type(), mysql_login_auth_switch_encode.type());
  EXPECT_EQ(mysql_login_auth_switch_decode.asAuthSwitchMessage().getAuthPluginName(), "");
}

/*
 * Negative Test the MYSQL Server Login Auth Switch message parser:
 * - incomplete auth plugin data
 */
TEST(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginData) {
  ClientLoginResponse mysql_login_auth_switch_encode{};
  mysql_login_auth_switch_encode.type(AuthSwitch);
  mysql_login_auth_switch_encode.asAuthSwitchMessage().setAuthPluginName(
      MySQLTestUtils::getAuthPluginName());
  mysql_login_auth_switch_encode.asAuthSwitchMessage().setAuthPluginData(
      MySQLTestUtils::getAuthPluginData20());
  Buffer::OwnedImpl buffer;
  mysql_login_auth_switch_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) +
      mysql_login_auth_switch_encode.asAuthSwitchMessage().getAuthPluginName().size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.type(), mysql_login_auth_switch_encode.type());
  EXPECT_EQ(mysql_login_auth_switch_decode.asAuthSwitchMessage().getAuthPluginName(),
            mysql_login_auth_switch_encode.asAuthSwitchMessage().getAuthPluginName());
  EXPECT_EQ(mysql_login_auth_switch_decode.asAuthSwitchMessage().getAuthPluginData(), "");
}

/*
 * Negative Test the MYSQL Server Auth More message parser:
 * - incomplete response code
 */
TEST(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompleteRespCode) {
  ClientLoginResponse mysql_login_auth_more_encode{};
  mysql_login_auth_more_encode.type(AuthMoreData);
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_more_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Auth More message parser:
 * - incomplete auth plugin name
 */
TEST(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompletePluginData) {
  ClientLoginResponse mysql_login_auth_more_encode{};
  mysql_login_auth_more_encode.type(AuthMoreData);
  mysql_login_auth_more_encode.asAuthMoreMessage().setAuthMoreData(
      MySQLTestUtils::getAuthPluginData20());
  Buffer::OwnedImpl buffer;
  mysql_login_auth_more_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_more_decode.type(), mysql_login_auth_more_encode.type());
  EXPECT_EQ(mysql_login_auth_more_decode.asAuthMoreMessage().getAuthMoreData(), "");
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
