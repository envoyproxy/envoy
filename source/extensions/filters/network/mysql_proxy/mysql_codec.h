#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int MYSQL_MAX_STR_SIZE = 256;
constexpr int MYSQL_PKT_SIZE = 1500;
constexpr int MYSQL_HDR_SIZE = 4;
constexpr int MYSQL_PROTOCOL_9 = 9;
constexpr int MYSQL_PROTOCOL_10 = 10;
constexpr int MYSQL_PKT_0 = 0;
constexpr int MYSQL_UNAME_PKT_NUM = 1;
constexpr int MYSQL_HDR_PKT_SIZE_MASK = 0x00FFFFFF;
constexpr int MYSQL_HDR_SEQ_MASK = 0x000000FF;
constexpr int MYSQL_LOGIN_RESP_PKT_NUM = 2;
constexpr int MYSQL_RESPONSE_PKT_NUM = 1;
constexpr int MAX_MYSQL_QUERY_STRING = 256;
constexpr int MAX_MYSQL_USER_STRING = 256;
constexpr int MIN_RESPONSE_PAYLOAD = 5;
constexpr int MYSQL_MAX_USER_LEN = 32;
constexpr int MYSQL_MAX_PASSWD_LEN = 32;
constexpr int MYSQL_RESP_OK = 0x00;
constexpr int MYSQL_RESP_MORE = 0x01;
constexpr int MYSQL_RESP_AUTH_SWITCH = 0xfe;
constexpr int MYSQL_RESP_ERR = 0xff;

constexpr int EOF_MARKER = 0xfe;
constexpr int ERR_MARKER = 0xff;

constexpr int CLIENT_CAP_FLD = 2;
constexpr int EXT_CLIENT_CAP_FLD = 2;
constexpr int MAX_PKT_FLD = 4;
constexpr int CHARSET_FLD = 1;
constexpr int UNAME_RSVD_STR = 23;

constexpr int FILLER_1_SIZE = 1;
constexpr int FILLER_2_SIZE = 2;
constexpr int FILLER_3_SIZE = 3;
constexpr int MYSQL_DEFAULT = 4;
constexpr int CHARACTER_SET_SIZE = 2;

constexpr int MAX_TABLE_COLUMNS = 64;
constexpr int MAX_TABLE_ROWS = 128;

constexpr int LAYOUT_CTLG = 0;
constexpr int LAYOUT_DB = 1;
constexpr int LAYOUT_TBL = 2;
constexpr int LAYOUT_ORG_TBL = 3;
constexpr int LAYOUT_NAME = 4;
constexpr int LAYOUT_ORG_NAME = 5;
constexpr int MYSQL_CATALOG_LAYOUT = 6;
constexpr int MULTI_CLIENT = 10;
constexpr int LOGIN_OK_SEQ = 2;
constexpr int GREETING_SEQ_NUM = 0;
constexpr int CHALLENGE_SEQ_NUM = 1;
constexpr int CHALLENGE_RESP_SEQ_NUM = 2;
constexpr int AUTH_SWITH_RESP_SEQ = 3;
constexpr int MYSQL_THREAD_ID = 0x5e;
constexpr int MYSQL_SERVER_CAPAB = 0x0101;
constexpr int MYSQL_SERVER_LANGUAGE = 0x21;
constexpr int MYSQL_SERVER_STATUS = 0x0200;
constexpr int MYSQL_SERVER_EXT_CAPAB = 0x0200;
constexpr int MYSQL_AUTHPLGIN = 0x00;
constexpr int MYSQL_UNSET = 0x00;
constexpr int MYSQL_UNSET_SIZE = 10;
constexpr int MYSQL_CLIENT_CONNECT_WITH_DB = 0x0008;
constexpr int MYSQL_CLIENT_CAPAB_41VS320 = 0x0200;
constexpr int MYSQL_CLIENT_CAPAB_SSL = 0x0800;
constexpr int MYSQL_EXT_CLIENT_CAPAB = 0x0300;
constexpr int MYSQL_EXT_CL_PLG_AUTH_CL_DATA = 0x0020;
constexpr int MYSQL_EXT_CL_SECURE_CONNECTION = 0x8000;
constexpr int MYSQL_MAX_PACKET = 0x00000001;
constexpr int MYSQL_CHARSET = 0x21;

constexpr uint8_t LENENCODINT_1BYTE = 0xfb;
constexpr uint8_t LENENCODINT_2BYTES = 0xfc;
constexpr uint8_t LENENCODINT_3BYTES = 0xfd;
constexpr uint8_t LENENCODINT_8BYTES = 0xfe;

constexpr int MYSQL_SUCCESS = 0;
constexpr int MYSQL_FAILURE = -1;
constexpr char MYSQL_STR_END = '\0';

class MySQLCodec : public Logger::Loggable<Logger::Id::filter> {
public:
  enum class PktType {
    MYSQL_REQUEST = 0,
    MYSQL_RESPONSE = 1,
  };

  enum class Cmd {
    COM_NULL = -1,
    COM_SLEEP = 0,
    COM_QUIT = 1,
    COM_INIT_DB = 2,
    COM_QUERY = 3,
    COM_FIELD_LIST = 4,
    COM_CREATE_DB = 5,
    COM_DROP_DB = 6,
    COM_REFRESH = 7,
    COM_SHUTDOWN = 8,
    COM_STATISTICS = 9,
    COM_PROCESS_INFO = 10,
    COM_CONNECT = 11,
    COM_PROCESS_KILL = 12,
    COM_DEBUG = 13,
    COM_PING = 14,
    COM_TIME = 15,
    COM_DELAYED_INSERT = 16,
    COM_CHANGE_USER = 17,
    COM_DAEMON = 29,
    COM_RESET_CONNECTION = 31,
  };

  PACKED_STRUCT(struct header_fields {
    uint32_t length : 24;
    uint32_t seq : 8;
  });

  union MySQLHeader {
    header_fields fields;
    uint32_t bits;
  };

  virtual int Decode(Buffer::Instance& data) PURE;
  virtual std::string Encode() PURE;
  virtual ~MySQLCodec(){};

  Cmd ParseCmd(Buffer::Instance& data);
  void BufUint8Add(Buffer::Instance& buffer, uint8_t val) { buffer.add(&val, sizeof(uint8_t)); }
  void BufUint16Add(Buffer::Instance& buffer, uint16_t val) { buffer.add(&val, sizeof(uint16_t)); }
  void BufUint32Add(Buffer::Instance& buffer, uint32_t val) { buffer.add(&val, sizeof(uint32_t)); }
  void BufStringAdd(Buffer::Instance& buffer, const std::string& str) { buffer.add(str); }
  std::string BufToString(Buffer::Instance& buffer);
  std::string EncodeHdr(const std::string& cmd_str, int seq);
  int GetSeq() { return seq_; }
  bool EndOfBuffer(Buffer::Instance& buffer);

  int BufUint8Drain(Buffer::Instance& buffer, uint8_t& val);
  int BufUint16Drain(Buffer::Instance& buffer, uint16_t& val);
  int BufUint32Drain(Buffer::Instance& buffer, uint32_t& val);
  int BufUint64Drain(Buffer::Instance& buffer, uint64_t& val);
  int BufReadBySizeDrain(Buffer::Instance& buffer, int len, int& val);
  int ReadLengthEncodedIntegerDrain(Buffer::Instance& buffer, int& val);
  int DrainBytes(Buffer::Instance& buffer, int skip_bytes);
  int BufStringDrain(Buffer::Instance& buffer, std::string& str);
  int BufStringDrainBySize(Buffer::Instance& buffer, std::string& str, int len);
  int HdrReadDrain(Buffer::Instance& buffer, int& len, int& seq);
  void SetSeq(int seq);

private:
  uint64_t offset_;
  int seq_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
