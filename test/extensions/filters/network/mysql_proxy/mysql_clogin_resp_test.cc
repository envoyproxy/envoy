#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
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

class MySQLCLoginRespTest : public testing::Test {
public:
  static ClientLoginResponse& getOkMessage() { return ok_; }
  static ClientLoginResponse& getErrMessage() { return err_; }
  static ClientLoginResponse& getOldAuthSwitchMessage() { return old_auth_switch_; }
  static ClientLoginResponse& getAuthSwitchMessage() { return auth_switch_; }
  static ClientLoginResponse& getAuthMoreMessage() { return auth_more_; }

private:
  static ClientLoginResponse initOkMessage() {
    ClientLoginResponse ok{};
    ok.initMessage(MYSQL_RESP_OK);
    ok.asOkMessage().setAffectedRows(1);
    ok.asOkMessage().setLastInsertId(MYSQL_UT_LAST_ID);
    ok.asOkMessage().setServerStatus(MYSQL_UT_SERVER_OK);
    ok.asOkMessage().setWarnings(MYSQL_UT_SERVER_WARNINGS);
    ok.asOkMessage().setInfo(MySQLTestUtils::getInfo());
    return ok;
  }
  static ClientLoginResponse initErrMessage() {
    ClientLoginResponse err{};
    err.initMessage(MYSQL_RESP_ERR);
    err.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
    err.asErrMessage().setSqlStateMarker('#');
    err.asErrMessage().setSqlState(MySQLTestUtils::getSqlState());
    err.asErrMessage().setErrorMessage(MySQLTestUtils::getErrorMessage());
    return err;
  }
  static ClientLoginResponse initOldAuthSwitchMessage() {
    ClientLoginResponse auth_switch{};
    auth_switch.initMessage(MYSQL_RESP_AUTH_SWITCH);
    auth_switch.asAuthSwitchMessage().setIsOldAuthSwitch(true);
    return auth_switch;
  }
  static ClientLoginResponse initAuthSwitchMessage() {
    ClientLoginResponse auth_switch{};
    auth_switch.initMessage(MYSQL_RESP_AUTH_SWITCH);
    auth_switch.asAuthSwitchMessage().setAuthPluginName(MySQLTestUtils::getAuthPluginName());
    auth_switch.asAuthSwitchMessage().setAuthPluginData(MySQLTestUtils::getAuthPluginData20());
    return auth_switch;
  }
  static ClientLoginResponse initAuthMoreMessage() {
    ClientLoginResponse auth_more{};
    auth_more.initMessage(MYSQL_RESP_MORE);
    auth_more.asAuthMoreMessage().setAuthMoreData(MySQLTestUtils::getAuthPluginData20());
    return auth_more;
  }

private:
  static ClientLoginResponse ok_;
  static ClientLoginResponse err_;
  static ClientLoginResponse auth_switch_;
  static ClientLoginResponse old_auth_switch_;
  static ClientLoginResponse auth_more_;
};

ClientLoginResponse MySQLCLoginRespTest::ok_ = MySQLCLoginRespTest::initOkMessage();
ClientLoginResponse MySQLCLoginRespTest::err_ = MySQLCLoginRespTest::initErrMessage();
ClientLoginResponse MySQLCLoginRespTest::auth_switch_ =
    MySQLCLoginRespTest::initAuthSwitchMessage();
ClientLoginResponse MySQLCLoginRespTest::old_auth_switch_ =
    MySQLCLoginRespTest::initOldAuthSwitchMessage();
ClientLoginResponse MySQLCLoginRespTest::auth_more_ = MySQLCLoginRespTest::initAuthMoreMessage();

/*
 * Test the MYSQL Server Login Response OK message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkEncDec) {
  ClientLoginResponse& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
  Buffer::OwnedImpl decode_data;
  mysql_loginok_encode.encode(decode_data);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getAffectedRows(),
            mysql_loginok_encode.asOkMessage().getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getLastInsertId(),
            mysql_loginok_encode.asOkMessage().getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getServerStatus(),
            mysql_loginok_encode.asOkMessage().getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getWarnings(),
            mysql_loginok_encode.asOkMessage().getWarnings());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getInfo(),
            mysql_loginok_encode.asOkMessage().getInfo());
}

/*
 * Test the MYSQL Server Login Response Err message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrEncDec) {
  ClientLoginResponse& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl decode_data;
  mysql_loginerr_encode.encode(decode_data);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlStateMarker(),
            mysql_loginerr_encode.asErrMessage().getSqlStateMarker());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlState(),
            mysql_loginerr_encode.asErrMessage().getSqlState());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorCode(),
            mysql_loginerr_encode.asErrMessage().getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorMessage(),
            mysql_loginerr_encode.asErrMessage().getErrorMessage());
}

/*
 * Test the MYSQL Server Login Old Auth Switch message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOldClientLoginResponseAuthSwitch) {
  ClientLoginResponse& mysql_old_auth_switch_encode =
      MySQLCLoginRespTest::getOldAuthSwitchMessage();
  Buffer::OwnedImpl decode_data;
  mysql_old_auth_switch_encode.encode(decode_data);

  ClientLoginResponse mysql_old_auth_switch_decode{};
  mysql_old_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_old_auth_switch_decode.asAuthSwitchMessage().getAuthPluginData(),
            mysql_old_auth_switch_encode.asAuthSwitchMessage().getAuthPluginData());
  EXPECT_EQ(mysql_old_auth_switch_decode.asAuthSwitchMessage().getAuthPluginName(),
            mysql_old_auth_switch_encode.asAuthSwitchMessage().getAuthPluginName());
}

/*
 * Test the MYSQL Server Login Auth Switch message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginClientLoginResponseAuthSwitch) {
  ClientLoginResponse& mysql_auth_switch_encode(MySQLCLoginRespTest::getAuthSwitchMessage());
  Buffer::OwnedImpl decode_data;
  mysql_auth_switch_encode.encode(decode_data);

  ClientLoginResponse mysql_auth_switch_decode{};
  mysql_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_auth_switch_decode.asAuthSwitchMessage().getAuthPluginData(),
            mysql_auth_switch_encode.asAuthSwitchMessage().getAuthPluginData());
  EXPECT_EQ(mysql_auth_switch_decode.asAuthSwitchMessage().getAuthPluginName(),
            mysql_auth_switch_encode.asAuthSwitchMessage().getAuthPluginName());
}

/*
 * Test the MYSQL Server Login Auth More message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMore) {
  ClientLoginResponse& mysql_auth_more_encode(MySQLCLoginRespTest::getAuthMoreMessage());
  Buffer::OwnedImpl decode_data;
  mysql_auth_more_encode.encode(decode_data);

  ClientLoginResponse mysql_auth_more_decode{};
  mysql_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_auth_more_decode.asAuthMoreMessage().getAuthMoreData(),
            mysql_auth_more_encode.asAuthMoreMessage().getAuthMoreData());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete response code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteRespCode) {
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), MYSQL_RESP_UNKNOWN);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete affected rows
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteAffectedRows) {
  ClientLoginResponse& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK last insert id
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteLastInsertId) {
  ClientLoginResponse& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) + MySQLTestUtils::sizeOfLengthEncodeInteger(
                                             mysql_loginok_encode.asOkMessage().getAffectedRows());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getAffectedRows(),
            mysql_loginok_encode.asOkMessage().getAffectedRows());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete server status
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteServerStatus) {
  ClientLoginResponse& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
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
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteWarnings) {
  ClientLoginResponse& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
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
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getAffectedRows(),
            mysql_loginok_encode.asOkMessage().getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getLastInsertId(),
            mysql_loginok_encode.asOkMessage().getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getServerStatus(),
            mysql_loginok_encode.asOkMessage().getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getWarnings(), 0);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete info
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteInfo) {
  ClientLoginResponse& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) +
                       MySQLTestUtils::sizeOfLengthEncodeInteger(
                           mysql_loginok_encode.asOkMessage().getAffectedRows()) +
                       MySQLTestUtils::sizeOfLengthEncodeInteger(
                           mysql_loginok_encode.asOkMessage().getLastInsertId()) +
                       sizeof(mysql_loginok_encode.asOkMessage().getServerStatus()) +
                       sizeof(mysql_loginok_encode.asOkMessage().getWarnings());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getAffectedRows(),
            mysql_loginok_encode.asOkMessage().getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getLastInsertId(),
            mysql_loginok_encode.asOkMessage().getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getServerStatus(),
            mysql_loginok_encode.asOkMessage().getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getWarnings(),
            mysql_loginok_encode.asOkMessage().getWarnings());
  EXPECT_EQ(mysql_loginok_decode.asOkMessage().getInfo(), "");
}

/*
 * Negative Test the MYSQL Server Login Err message parser:
 * - incomplete response code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteRespCode) {
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), MYSQL_RESP_UNKNOWN);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete error code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorcode) {
  ClientLoginResponse& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorCode(), 0);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete sql state marker
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteStateMarker) {
  ClientLoginResponse& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) + sizeof(mysql_loginerr_encode.asErrMessage().getErrorCode());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getErrorCode(),
            mysql_loginerr_encode.asErrMessage().getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.asErrMessage().getSqlStateMarker(), 0);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete sql state
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteSqlState) {
  ClientLoginResponse& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) +
                       sizeof(mysql_loginerr_encode.asErrMessage().getErrorCode()) +
                       sizeof(mysql_loginerr_encode.asErrMessage().getSqlStateMarker());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorMessage) {
  ClientLoginResponse& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
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
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompleteRespCode) {
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.getRespCode(), MYSQL_RESP_UNKNOWN);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete auth plugin name
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginName) {
  ClientLoginResponse& mysql_login_auth_switch_encode = MySQLCLoginRespTest::getAuthSwitchMessage();
  Buffer::OwnedImpl buffer;
  mysql_login_auth_switch_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) +
      mysql_login_auth_switch_encode.asAuthSwitchMessage().getAuthPluginName().size() - 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.getRespCode(),
            mysql_login_auth_switch_encode.getRespCode());
  EXPECT_EQ(mysql_login_auth_switch_decode.asAuthSwitchMessage().getAuthPluginName(), "");
}

/*
 * Negative Test the MYSQL Server Login Auth Switch message parser:
 * - incomplete auth plugin data
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginData) {
  ClientLoginResponse& mysql_login_auth_switch_encode = MySQLCLoginRespTest::getAuthSwitchMessage();
  Buffer::OwnedImpl buffer;
  mysql_login_auth_switch_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) +
      mysql_login_auth_switch_encode.asAuthSwitchMessage().getAuthPluginName().size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.getRespCode(),
            mysql_login_auth_switch_encode.getRespCode());
  EXPECT_EQ(mysql_login_auth_switch_decode.asAuthSwitchMessage().getAuthPluginName(),
            mysql_login_auth_switch_encode.asAuthSwitchMessage().getAuthPluginName());
  EXPECT_EQ(mysql_login_auth_switch_decode.asAuthSwitchMessage().getAuthPluginData(), "");
}

/*
 * Negative Test the MYSQL Server Auth More message parser:
 * - incomplete response code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompleteRespCode) {
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_more_decode.getRespCode(), MYSQL_RESP_UNKNOWN);
}

/*
 * Negative Test the MYSQL Server Auth More message parser:
 * - incomplete auth plugin name
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompletePluginData) {
  ClientLoginResponse& mysql_login_auth_more_encode = MySQLCLoginRespTest::getAuthMoreMessage();
  Buffer::OwnedImpl buffer;
  mysql_login_auth_more_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_more_decode.getRespCode(), mysql_login_auth_more_encode.getRespCode());
  EXPECT_EQ(mysql_login_auth_more_decode.asAuthMoreMessage().getAuthMoreData(), "");
}

// /*
//  * Test type convert:
//  */
// TEST_F(MySQLCLoginRespTest, MySQLLoginRespTypeConvert) {

// }

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
