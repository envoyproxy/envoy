#pragma once

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr uint16_t MYSQL_MAX_STR_SIZE = 256;
constexpr uint16_t MYSQL_PKT_SIZE = 1500;
constexpr uint8_t MYSQL_HDR_SIZE = 4;
constexpr uint8_t MYSQL_PROTOCOL_9 = 9;
constexpr uint8_t MYSQL_PROTOCOL_10 = 10;
constexpr uint8_t MYSQL_PKT_0 = 0;
constexpr uint8_t MYSQL_UNAME_PKT_NUM = 1;
constexpr uint32_t MYSQL_HDR_PKT_SIZE_MASK = 0x00FFFFFF;
constexpr uint32_t MYSQL_HDR_SEQ_MASK = 0x000000FF;
constexpr uint8_t MYSQL_LOGIN_RESP_PKT_NUM = 2;
constexpr uint8_t MYSQL_REQUEST_PKT_NUM = 0;
constexpr uint8_t MYSQL_RESPONSE_PKT_NUM = 1;
constexpr uint16_t MAX_MYSQL_QUERY_STRING = 256;
constexpr uint16_t MAX_MYSQL_USER_STRING = 256;
constexpr uint8_t MIN_RESPONSE_PAYLOAD = 5;
constexpr uint8_t MYSQL_MAX_USER_LEN = 32;
constexpr uint8_t MYSQL_MAX_PASSWD_LEN = 32;

constexpr uint8_t MYSQL_RESP_OK = 0x00;
constexpr uint8_t MYSQL_RESP_MORE = 0x01;
constexpr uint8_t MYSQL_RESP_AUTH_SWITCH = 0xfe;
constexpr uint8_t MYSQL_RESP_ERR = 0xff;

constexpr uint8_t EOF_MARKER = 0xfe;
constexpr uint8_t ERR_MARKER = 0xff;

constexpr uint8_t CLIENT_CAP_FLD = 2;
constexpr uint8_t EXT_CLIENT_CAP_FLD = 2;
constexpr uint8_t MAX_PKT_FLD = 4;
constexpr uint8_t CHARSET_FLD = 1;
constexpr uint8_t UNAME_RSVD_STR = 23;

constexpr uint8_t FILLER_1_SIZE = 1;
constexpr uint8_t FILLER_2_SIZE = 2;
constexpr uint8_t FILLER_3_SIZE = 3;
constexpr uint8_t MYSQL_DEFAULT = 4;
constexpr uint8_t CHARACTER_SET_SIZE = 2;

constexpr uint8_t MAX_TABLE_COLUMNS = 64;
constexpr uint8_t MAX_TABLE_ROWS = 128;

constexpr uint8_t LAYOUT_CTLG = 0;
constexpr uint8_t LAYOUT_DB = 1;
constexpr uint8_t LAYOUT_TBL = 2;
constexpr uint8_t LAYOUT_ORG_TBL = 3;
constexpr uint8_t LAYOUT_NAME = 4;
constexpr uint8_t LAYOUT_ORG_NAME = 5;
constexpr uint8_t MYSQL_CATALOG_LAYOUT = 6;
constexpr uint8_t MULTI_CLIENT = 10;
constexpr uint8_t LOGIN_OK_SEQ = 2;
constexpr uint8_t GREETING_SEQ_NUM = 0;
constexpr uint8_t CHALLENGE_SEQ_NUM = 1;
constexpr uint8_t CHALLENGE_RESP_SEQ_NUM = 2;
constexpr uint8_t AUTH_SWITH_RESP_SEQ = 3;
constexpr uint32_t MYSQL_THREAD_ID = 0x5e;
constexpr uint16_t MYSQL_SERVER_CAPAB = 0x0101;
constexpr uint8_t MYSQL_SERVER_LANGUAGE = 0x21;
constexpr uint16_t MYSQL_SERVER_STATUS = 0x0200;
constexpr uint16_t MYSQL_SERVER_EXT_CAPAB = 0x0200;
constexpr uint16_t MYSQL_EXT_CLIENT_CAPAB = 0x0300;

constexpr uint32_t CLIENT_PLUGIN_AUTH = 0x00080000;
constexpr uint32_t CLIENT_SECURE_CONNECTION = 0x8000;
constexpr uint32_t CLIENT_LONG_PASSWORD = 0x00000001;
constexpr uint32_t CLIENT_TRANSACTIONS = 0x00002000;
constexpr uint32_t CLIENT_LOCAL_FILES = 0x00000080;
constexpr uint32_t CLIENT_MULTI_STATEMENTS = 0x00010000;
constexpr uint32_t CLIENT_LONG_FLAG = 0x00000004;
constexpr uint32_t CLIENT_IGNORE_SIGPIPE = 4096;
constexpr uint32_t CLIENT_INTERACTIVE = 1024;
constexpr uint32_t CLIENT_FOUND_ROWS = 0x00000002;
constexpr uint32_t CLIENT_IGNORE_SPACE = 256;
constexpr uint32_t CLIENT_PROTOCOL_41 = 0x00000200;
constexpr uint32_t CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA = 0x00200000;
constexpr uint32_t CLIENT_CONNECT_WITH_DB = 0x00000008;
constexpr uint32_t CLIENT_CONNECT_ATTRS = 0x00100000;
constexpr uint32_t CLIENT_SSL = 0x00000800;
constexpr uint16_t MYSQL_EXT_CL_PLUGIN_AUTH = 0x8;
constexpr uint32_t MYSQL_MAX_PACKET = 0x00000001;
constexpr uint8_t MYSQL_CHARSET = 0x21;
constexpr uint8_t DEFAULT_MYSQL_CHARSET = 45; // utf8mb4
constexpr uint16_t DEFALUT_MYSQL_SERVER_STATUS = 2;
constexpr uint8_t MYSQL_SQL_STATE_LEN = 5;
constexpr int NATIVE_PSSWORD_HASH_LENGTH = 20;
constexpr int OLD_PASSWORD_HASH_LENGTH = 8;

constexpr uint8_t LENENCODINT_1BYTE = 0xfb;
constexpr uint8_t LENENCODINT_2BYTES = 0xfc;
constexpr uint8_t LENENCODINT_3BYTES = 0xfd;
constexpr uint8_t LENENCODINT_8BYTES = 0xfe;

constexpr uint32_t DEFAULT_MAX_PACKET_SIZE = (1 << 24) - 1; // 16M-1
constexpr uint8_t MIN_PROTOCOL_VERSION = 10;

constexpr char MYSQL_STR_END = '\0';

// error code
constexpr uint16_t MYSQL_CR_AUTH_PLUGIN_ERR = 2061;
constexpr uint16_t ER_USERNAME = 1468;
constexpr uint16_t ER_NOT_SUPPORTED_AUTH_MODE = 1251;
constexpr uint16_t ER_PASSWORD_NO_MATCH = 1133;
constexpr uint16_t ER_NET_PACKETS_OUT_OF_ORDER = 1156;
constexpr uint16_t ER_PASSWD_LENGTH = 1372;
constexpr uint16_t ER_ACCESS_DENIED_ERROR = 1045;
constexpr uint16_t ER_ER_BAD_DB_ERROR = 1049;
constexpr uint8_t MYSQL_SQL_STATE_MARKER = '#';

enum DecodeStatus : uint8_t {
  Success = 0,
  Failure = 1,
};

class MySQLCodec : public Logger::Loggable<Logger::Id::filter> {
public:
  enum class PktType {
    MysqlRequest = 0,
    MysqlResponse = 1,
  };

  virtual ~MySQLCodec() = default;

  uint8_t getSeq() const { return seq_; }
  void setSeq(uint8_t seq) { seq_ = seq; }

  DecodeStatus decode(Buffer::Instance& data, uint8_t seq, uint32_t len) {
    seq_ = seq;
    return parseMessage(data, len);
  }

protected:
  friend class MySQLTestUtils;
  virtual DecodeStatus parseMessage(Buffer::Instance& data, uint32_t len) PURE;
  virtual void encode(Buffer::Instance& out) const PURE;
  uint8_t seq_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
