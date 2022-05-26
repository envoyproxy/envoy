#include "source/common/buffer/buffer_impl.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
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
  static OkMessage& getOkMessage() { return ok_; }
  static ErrMessage& getErrMessage() { return err_; }
  static AuthSwitchMessage& getOldAuthSwitchMessage() { return old_auth_switch_; }
  static AuthSwitchMessage& getAuthSwitchMessage() { return auth_switch_; }
  static AuthMoreMessage& getAuthMoreMessage() { return auth_more_; }

private:
  static OkMessage initOkMessage() {
    OkMessage ok{};
    ok.setAffectedRows(1);
    ok.setLastInsertId(MYSQL_UT_LAST_ID);
    ok.setServerStatus(MYSQL_UT_SERVER_OK);
    ok.setWarnings(MYSQL_UT_SERVER_WARNINGS);
    ok.setInfo(MySQLTestUtils::getInfo());
    return ok;
  }
  static ErrMessage initErrMessage() {
    ErrMessage err{};
    err.setErrorCode(MYSQL_ERROR_CODE);
    err.setSqlStateMarker('#');
    err.setSqlState(MySQLTestUtils::getSqlState());
    err.setErrorMessage(MySQLTestUtils::getErrorMessage());
    return err;
  }
  static AuthSwitchMessage initOldAuthSwitchMessage() {
    AuthSwitchMessage auth_switch{};
    auth_switch.setIsOldAuthSwitch(true);
    return auth_switch;
  }
  static AuthSwitchMessage initAuthSwitchMessage() {
    AuthSwitchMessage auth_switch{};
    auth_switch.setAuthPluginName(MySQLTestUtils::getAuthPluginName());
    auth_switch.setAuthPluginData(MySQLTestUtils::getAuthPluginData20());
    return auth_switch;
  }
  static AuthMoreMessage initAuthMoreMessage() {
    AuthMoreMessage auth_more{};
    auth_more.setAuthMoreData(MySQLTestUtils::getAuthPluginData20());
    return auth_more;
  }

private:
  static OkMessage ok_;
  static ErrMessage err_;
  static AuthSwitchMessage auth_switch_;
  static AuthSwitchMessage old_auth_switch_;
  static AuthMoreMessage auth_more_;
};

OkMessage MySQLCLoginRespTest::ok_ = MySQLCLoginRespTest::initOkMessage();
ErrMessage MySQLCLoginRespTest::err_ = MySQLCLoginRespTest::initErrMessage();
AuthSwitchMessage MySQLCLoginRespTest::auth_switch_ = MySQLCLoginRespTest::initAuthSwitchMessage();
AuthSwitchMessage MySQLCLoginRespTest::old_auth_switch_ =
    MySQLCLoginRespTest::initOldAuthSwitchMessage();
AuthMoreMessage MySQLCLoginRespTest::auth_more_ = MySQLCLoginRespTest::initAuthMoreMessage();

/*
 * Test the MYSQL Server Login Response OK message parser:
 * - message is encoded using the OkMessage class
 * - message is decoded using the OkMessage class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkEncDec) {
  OkMessage& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();

  Buffer::OwnedImpl decode_data;
  mysql_loginok_encode.encode(decode_data);

  OkMessage mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), mysql_loginok_encode.getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.getWarnings(), mysql_loginok_encode.getWarnings());
  EXPECT_EQ(mysql_loginok_decode.getInfo(), mysql_loginok_encode.getInfo());
}

/*
 * Test the MYSQL Server Login Response Err message parser:
 * - message is encoded using the ErrMessage class
 * - message is decoded using the ErrMessage class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrEncDec) {
  ErrMessage& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl decode_data;
  mysql_loginerr_encode.encode(decode_data);

  ErrMessage mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getSqlStateMarker(), mysql_loginerr_encode.getSqlStateMarker());
  EXPECT_EQ(mysql_loginerr_decode.getSqlState(), mysql_loginerr_encode.getSqlState());
  EXPECT_EQ(mysql_loginerr_decode.getErrorCode(), mysql_loginerr_encode.getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.getErrorMessage(), mysql_loginerr_encode.getErrorMessage());
}

/*
 * Test the MYSQL Server Login Old Auth Switch message parser:
 * - message is encoded using the AuthSwitchMessage class
 * - message is decoded using the AuthSwitchMessage class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOldClientLoginResponseAuthSwitch) {
  AuthSwitchMessage& mysql_old_auth_switch_encode = MySQLCLoginRespTest::getOldAuthSwitchMessage();

  Buffer::OwnedImpl decode_data;
  mysql_old_auth_switch_encode.encode(decode_data);

  AuthSwitchMessage mysql_old_auth_switch_decode{};
  mysql_old_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_old_auth_switch_decode.getAuthPluginData(),
            mysql_old_auth_switch_encode.getAuthPluginData());
  EXPECT_EQ(mysql_old_auth_switch_decode.getAuthPluginName(),
            mysql_old_auth_switch_encode.getAuthPluginName());
}

/*
 * Test the MYSQL Server Login Auth Switch message parser:
 * - message is encoded using the AuthSwitchMessage class
 * - message is decoded using the AuthSwitchMessage class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginClientLoginResponseAuthSwitch) {
  AuthSwitchMessage& mysql_auth_switch_encode(MySQLCLoginRespTest::getAuthSwitchMessage());
  Buffer::OwnedImpl decode_data;
  mysql_auth_switch_encode.encode(decode_data);

  AuthSwitchMessage mysql_auth_switch_decode{};
  mysql_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_auth_switch_decode.getAuthPluginData(),
            mysql_auth_switch_encode.getAuthPluginData());
  EXPECT_EQ(mysql_auth_switch_decode.getAuthPluginName(),
            mysql_auth_switch_encode.getAuthPluginName());
}

/*
 * Test the MYSQL Server Login Auth More message parser:
 * - message is encoded using the AuthMoreMessage class
 * - message is decoded using the AuthMoreMessage class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMore) {
  AuthMoreMessage& mysql_auth_more_encode(MySQLCLoginRespTest::getAuthMoreMessage());

  Buffer::OwnedImpl decode_data;
  mysql_auth_more_encode.encode(decode_data);

  AuthMoreMessage mysql_auth_more_decode{};
  mysql_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_auth_more_decode.getAuthMoreData(), mysql_auth_more_encode.getAuthMoreData());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete response code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteRespCode) {
  OkMessage mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();

  Buffer::OwnedImpl decode_data;

  OkMessage mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), MYSQL_RESP_OK);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete affected rows
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteAffectedRows) {
  OkMessage& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();

  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  OkMessage mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK last insert id
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteLastInsertId) {
  OkMessage& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();

  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) + MySQLTestUtils::sizeOfLengthEncodeInteger(
                                             mysql_loginok_encode.getAffectedRows());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  OkMessage mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete server status
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteServerStatus) {
  OkMessage& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();

  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) +
      MySQLTestUtils::sizeOfLengthEncodeInteger(mysql_loginok_encode.getAffectedRows()) +
      MySQLTestUtils::sizeOfLengthEncodeInteger(mysql_loginok_encode.getLastInsertId());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  OkMessage mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), 0);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete warnings
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteWarnings) {
  OkMessage& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) +
      MySQLTestUtils::sizeOfLengthEncodeInteger(mysql_loginok_encode.getAffectedRows()) +
      MySQLTestUtils::sizeOfLengthEncodeInteger(mysql_loginok_encode.getLastInsertId()) +
      sizeof(mysql_loginok_encode.getServerStatus());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  OkMessage mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), mysql_loginok_encode.getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.getWarnings(), 0);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete info
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteInfo) {
  OkMessage& mysql_loginok_encode = MySQLCLoginRespTest::getOkMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginok_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) +
      MySQLTestUtils::sizeOfLengthEncodeInteger(mysql_loginok_encode.getAffectedRows()) +
      MySQLTestUtils::sizeOfLengthEncodeInteger(mysql_loginok_encode.getLastInsertId()) +
      sizeof(mysql_loginok_encode.getServerStatus()) + sizeof(mysql_loginok_encode.getWarnings()) +
      mysql_loginok_encode.getInfo().size() + 1;

  OkMessage mysql_loginok_decode{};
  mysql_loginok_decode.decode(buffer, CHALLENGE_SEQ_NUM, incomplete_len);
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), mysql_loginok_encode.getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.getWarnings(), mysql_loginok_encode.getWarnings());
  EXPECT_EQ(mysql_loginok_decode.getInfo(), "");
}

/*
 * Negative Test the MYSQL Server Login Err message parser:
 * - incomplete response code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteRespCode) {
  Buffer::OwnedImpl decode_data;

  ErrMessage mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), MYSQL_RESP_ERR);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete error code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorcode) {
  ErrMessage& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ErrMessage mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
  EXPECT_EQ(mysql_loginerr_decode.getErrorCode(), 0);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete sql state marker
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteStateMarker) {
  ErrMessage& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) + sizeof(mysql_loginerr_encode.getErrorCode());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ErrMessage mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
  EXPECT_EQ(mysql_loginerr_decode.getErrorCode(), mysql_loginerr_encode.getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.getSqlStateMarker(), 0);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete sql state
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteSqlState) {
  ErrMessage& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) + sizeof(mysql_loginerr_encode.getErrorCode()) +
                       sizeof(mysql_loginerr_encode.getSqlStateMarker());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ErrMessage mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
  EXPECT_EQ(mysql_loginerr_decode.getErrorCode(), mysql_loginerr_encode.getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.getSqlStateMarker(), mysql_loginerr_encode.getSqlStateMarker());
  EXPECT_EQ(mysql_loginerr_decode.getSqlState(), "");
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete error message
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorMessage) {
  ErrMessage& mysql_loginerr_encode = MySQLCLoginRespTest::getErrMessage();
  Buffer::OwnedImpl buffer;
  mysql_loginerr_encode.setErrorMessage(MySQLTestUtils::getErrorMessage());
  mysql_loginerr_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) + sizeof(mysql_loginerr_encode.getErrorCode()) +
                       sizeof(mysql_loginerr_encode.getSqlStateMarker()) +
                       mysql_loginerr_encode.getSqlState().size() +
                       mysql_loginerr_encode.getErrorMessage().size() + 1;

  ErrMessage mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(buffer, CHALLENGE_SEQ_NUM, incomplete_len);
  EXPECT_EQ(mysql_loginerr_decode.getRespCode(), mysql_loginerr_encode.getRespCode());
  EXPECT_EQ(mysql_loginerr_decode.getErrorCode(), mysql_loginerr_encode.getErrorCode());
  EXPECT_EQ(mysql_loginerr_decode.getSqlStateMarker(), mysql_loginerr_encode.getSqlStateMarker());
  EXPECT_EQ(mysql_loginerr_decode.getSqlState(), mysql_loginerr_encode.getSqlState());
  EXPECT_EQ(mysql_loginerr_decode.getErrorMessage(), "");
}

/*
 * Negative Test the MYSQL Server Login Auth Switch message parser:
 * - incomplete response code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompleteRespCode) {
  Buffer::OwnedImpl decode_data;

  AuthSwitchMessage mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.getRespCode(), MYSQL_RESP_AUTH_SWITCH);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete auth plugin name
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginName) {
  AuthSwitchMessage& mysql_login_auth_switch_encode = MySQLCLoginRespTest::getAuthSwitchMessage();
  Buffer::OwnedImpl buffer;
  mysql_login_auth_switch_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint8_t) + mysql_login_auth_switch_encode.getAuthPluginName().size() - 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  AuthSwitchMessage mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.getRespCode(),
            mysql_login_auth_switch_encode.getRespCode());
  EXPECT_EQ(mysql_login_auth_switch_decode.getAuthPluginName(), "");
}

/*
 * Negative Test the MYSQL Server Login Auth Switch message parser:
 * - incomplete auth plugin data
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginData) {
  AuthSwitchMessage& mysql_login_auth_switch_encode = MySQLCLoginRespTest::getAuthSwitchMessage();
  Buffer::OwnedImpl buffer;
  mysql_login_auth_switch_encode.encode(buffer);

  AuthSwitchMessage mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(buffer, CHALLENGE_SEQ_NUM, buffer.length() + 1);
  EXPECT_EQ(mysql_login_auth_switch_decode.getRespCode(),
            mysql_login_auth_switch_encode.getRespCode());
  EXPECT_EQ(mysql_login_auth_switch_decode.getAuthPluginName(),
            mysql_login_auth_switch_encode.getAuthPluginName());
  EXPECT_EQ(mysql_login_auth_switch_decode.getAuthPluginData().size(), 0);
}

/*
 * Negative Test the MYSQL Server Auth More message parser:
 * - incomplete response code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompleteRespCode) {
  Buffer::OwnedImpl decode_data;

  AuthMoreMessage mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_more_decode.getRespCode(), MYSQL_RESP_MORE);
}

/*
 * Negative Test the MYSQL Server Auth More message parser:
 * - incomplete auth plugin name
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompletePluginData) {
  AuthMoreMessage& mysql_login_auth_more_encode = MySQLCLoginRespTest::getAuthMoreMessage();

  Buffer::OwnedImpl buffer;
  mysql_login_auth_more_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t) + mysql_login_auth_more_encode.getAuthMoreData().size() + 1;

  AuthMoreMessage mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(buffer, CHALLENGE_SEQ_NUM, incomplete_len);
  EXPECT_EQ(mysql_login_auth_more_decode.getRespCode(), mysql_login_auth_more_encode.getRespCode());
  EXPECT_EQ(mysql_login_auth_more_decode.getAuthMoreData().size(), 0);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
