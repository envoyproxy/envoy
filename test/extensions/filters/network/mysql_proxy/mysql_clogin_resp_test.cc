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
  ClientLoginResponse getOkMessage() const {
    ClientLoginResponse ok{};
    ok.type(Ok);
    ok.asOkMessage().setAffectedRows(1);
    ok.asOkMessage().setLastInsertId(MYSQL_UT_LAST_ID);
    ok.asOkMessage().setServerStatus(MYSQL_UT_SERVER_OK);
    ok.asOkMessage().setWarnings(MYSQL_UT_SERVER_WARNINGS);
    return ok;
  }
  ClientLoginResponse getErrMessage() const {
    ClientLoginResponse err{};
    err.type(Err);
    err.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
    err.asErrMessage().setSqlStateMarker('#');
    err.asErrMessage().setSqlState(MySQLTestUtils::getSqlState());
    err.asErrMessage().setErrorMessage(MySQLTestUtils::getErrorMessage());
    return err;
  }
  ClientLoginResponse getOldAuthSwithMessage() const {
    ClientLoginResponse auth_switch{};
    auth_switch.type(AuthSwitch);
    return auth_switch;
  }
  ClientLoginResponse getAuthSwithMessage() const {
    ClientLoginResponse auth_switch{};
    auth_switch.type(AuthSwitch);
    auth_switch.asAuthSwitchMessage().setAuthPluginName(MySQLTestUtils::getAuthPluginName());
    auth_switch.asAuthSwitchMessage().setAuthPluginData(MySQLTestUtils::getAuthPluginData20());
    return auth_switch;
  }
  ClientLoginResponse getAuthMoreMessage() const {
    ClientLoginResponse auth_more{};
    auth_more.type(AuthMoreData);
    auth_more.asAuthMoreMessage().setAuthMoreData(MySQLTestUtils::getAuthPluginData20());
    return auth_more;
  }
};

/*
 * Test the MYSQL Server Login Response OK message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkEncDec) {
  ClientLoginResponse mysql_loginok_encode = getOkMessage();
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
TEST_F(MySQLCLoginRespTest, MySQLLoginErrEncDec) {
  ClientLoginResponse mysql_loginerr_encode = getErrMessage();
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
TEST_F(MySQLCLoginRespTest, MySQLLoginOldAuthSwitch) {
  ClientLoginResponse mysql_old_auth_switch_encode(getOldAuthSwithMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitch) {
  ClientLoginResponse mysql_auth_switch_encode(getAuthSwithMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMore) {
  ClientLoginResponse mysql_auth_more_encode(getAuthMoreMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteRespCode) {
  ClientLoginResponse mysql_loginok_encode(getOkMessage());
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginok_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete affected rows
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteAffectedRows) {
  ClientLoginResponse mysql_loginok_encode(getOkMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteLastInsertId) {
  ClientLoginResponse mysql_loginok_encode(getOkMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteServerStatus) {
  ClientLoginResponse mysql_loginok_encode(getOkMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginOkIncompleteWarnings) {
  ClientLoginResponse mysql_loginok_encode(getOkMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteRespCode) {
  ClientLoginResponse mysql_loginerr_encode(getErrMessage());
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_loginerr_decode{};
  mysql_loginerr_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_loginerr_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete error code
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorcode) {
  ClientLoginResponse mysql_loginerr_encode(getErrMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteStateMarker) {
  ClientLoginResponse mysql_loginerr_encode(getErrMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteSqlState) {
  ClientLoginResponse mysql_loginerr_encode(getErrMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginErrIncompleteErrorMessage) {
  ClientLoginResponse mysql_loginerr_encode(getErrMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompleteRespCode) {
  ClientLoginResponse mysql_login_auth_switch_encode(getAuthSwithMessage());
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_login_auth_switch_decode{};
  mysql_login_auth_switch_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_switch_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Login ERR message parser:
 * - incomplete auth plugin name
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginName) {
  ClientLoginResponse mysql_login_auth_switch_encode(getAuthSwithMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthSwitchIncompletePluginData) {
  ClientLoginResponse mysql_login_auth_switch_encode(getAuthSwithMessage());
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
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompleteRespCode) {
  ClientLoginResponse mysql_login_auth_more_encode(getAuthMoreMessage());
  Buffer::OwnedImpl decode_data;

  ClientLoginResponse mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_more_decode.type(), Null);
}

/*
 * Negative Test the MYSQL Server Auth More message parser:
 * - incomplete auth plugin name
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginAuthMoreIncompletePluginData) {
  ClientLoginResponse mysql_login_auth_more_encode(getAuthMoreMessage());
  Buffer::OwnedImpl buffer;
  mysql_login_auth_more_encode.encode(buffer);

  int incomplete_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLoginResponse mysql_login_auth_more_decode{};
  mysql_login_auth_more_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_login_auth_more_decode.type(), mysql_login_auth_more_encode.type());
  EXPECT_EQ(mysql_login_auth_more_decode.asAuthMoreMessage().getAuthMoreData(), "");
}

/*
 * Test type convert:
 */
TEST_F(MySQLCLoginRespTest, MySQLLoginRespTypeConvert) {
  ClientLoginResponse ok = getOkMessage();
  ClientLoginResponse err = getErrMessage();
  ClientLoginResponse auth_switch = getAuthSwithMessage();
  ClientLoginResponse auth_more = getAuthMoreMessage();
  {
    ClientLoginResponse ok2err = ok;
    ok2err = err;
    EXPECT_EQ(ok2err, err);
  }
  {
    ClientLoginResponse ok2auth_swith = ok;
    ok2auth_swith = auth_switch;
    EXPECT_EQ(ok2auth_swith, auth_switch);
  }
  {
    ClientLoginResponse ok2auth_more = ok;
    ok2auth_more = auth_more;
    EXPECT_EQ(ok2auth_more, auth_more);
  }
  {
    ClientLoginResponse err2ok = err;
    err2ok = ok;
    EXPECT_EQ(err2ok, ok);
    err2ok.type(Err);
    // err message storage should be reconstruct
    EXPECT_EQ(err2ok.asErrMessage().getSqlState(), "");
    EXPECT_EQ(err2ok.asErrMessage().getErrorMessage(), "");
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
