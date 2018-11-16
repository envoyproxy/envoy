#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLCodec : public Logger::Loggable<Logger::Id::filter> {
#define MYSQL_MAX_STR_SIZE 256
#define MYSQL_PKT_SIZE 1500
#define MYSQL_HDR_SIZE 4
#define MYSQL_PROTOCOL_9 9
#define MYSQL_PROTOCOL_10 10
#define MYSQL_PKT_0 0
#define MYSQL_UNAME_PKT_NUM 1
#define MYSQL_HDR_PKT_SIZE_MASK 0x00FFFFFF
#define MYSQL_HDR_SEQ_MASK 0x000000FF
#define MYSQL_LOGIN_RESP_PKT_NUM 2
#define MYSQL_RESPONSE_PKT_NUM 1
#define MAX_MYSQL_QUERY_STRING 256
#define MAX_MYSQL_USER_STRING 256
#define MIN_RESPONSE_PAYLOAD 5
#define MYSQL_MAX_USER_LEN 32
#define MYSQL_MAX_PASSWD_LEN 32
#define MYSQL_RESP_OK 0x00
#define MYSQL_RESP_MORE 0x01
#define MYSQL_RESP_AUTH_SWITCH 0xfe
#define MYSQL_RESP_ERR 0xff

#define EOF_MARKER 0xfe
#define ERR_MARKER 0xff

#define CLIENT_CAP_FLD 2
#define EXT_CLIENT_CAP_FLD 2
#define MAX_PKT_FLD 4
#define CHARSET_FLD 1
#define UNAME_RSVD_STR 23

#define FILLER_1_SIZE 1
#define FILLER_2_SIZE 2
#define FILLER_3_SIZE 3
#define MYSQL_DEFAULT 4
#define CHARACTER_SET_SIZE 2

#define MAX_TABLE_COLUMNS 64
#define MAX_TABLE_ROWS 128

#define LAYOUT_CTLG 0
#define LAYOUT_DB 1
#define LAYOUT_TBL 2
#define LAYOUT_ORG_TBL 3
#define LAYOUT_NAME 4
#define LAYOUT_ORG_NAME 5
#define MYSQL_CATALOG_LAYOUT 6

#define OK_MESSAGE "\x00\x00"
#define KO_MESSAGE "\x00\x01"
#define MYSQL_STR_DEL "\x00"
#define MULTI_CLIENT 10
#define LOGIN_OK_SEQ 2

#define GREETING_SEQ_NUM 0
#define CHALLENGE_SEQ_NUM 1
#define CHALLENGE_RESP_SEQ_NUM 2
#define AUTH_SWITH_RESP_SEQ 3

#define MYSQL_THREAD_ID 0x5e
#define MYSQL_SERVER_CAPAB 0x0101
#define MYSQL_SERVER_LANGUAGE 0x21
#define MYSQL_SERVER_STATUS 0x0200
#define MYSQL_SERVER_EXT_CAPAB 0x0200
#define MYSQL_AUTHPLGIN 0x00
#define MYSQL_UNSET 0x00
#define MYSQL_UNSET_SIZE 10
#define MYSQL_CLIENT_CONNECT_WITH_DB 0x0008
#define MYSQL_CLIENT_CAPAB_41VS320 0x0200
#define MYSQL_CLIENT_CAPAB_SSL 0x0800
#define MYSQL_EXT_CLIENT_CAPAB 0x0300
#define MYSQL_EXT_CL_PLG_AUTH_CL_DATA 0x0020
#define MYSQL_EXT_CL_SECURE_CONNECTION 0x8000
#define MYSQL_MAX_PACKET 0x00000001
#define MYSQL_CHARSET 0x21

#define LENENCODINT_1BYTE 0xfb
#define LENENCODINT_2BYTES 0xfc
#define LENENCODINT_3BYTES 0xfd
#define LENENCODINT_8BYTES 0xfe

#define MYSQL_SUCCESS 0
#define MYSQL_FAILURE (-1)
#define MYSQL_STR_END '\0'

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

  MySQLCodec::Cmd ParseCmd(Buffer::Instance& data);
  MySQLCodec::PktType CheckPktType(Buffer::Instance& data);
  int BufUint8Drain(Buffer::Instance& buffer, uint8_t& val);
  int BufUint16Drain(Buffer::Instance& buffer, uint16_t& val);
  int BufUint32Drain(Buffer::Instance& buffer, uint32_t& val);
  int BufUint64Drain(Buffer::Instance& buffer, uint64_t& val);
  int BufReadBySizeDrain(Buffer::Instance& buffer, int len, int& val);
  int ReadLengthEncodedIntegerDrain(Buffer::Instance& buffer, int& val);
  void BufUint8Add(Buffer::Instance& buffer, uint8_t val) { buffer.add(&val, sizeof(uint8_t)); }
  void BufUint16Add(Buffer::Instance& buffer, uint16_t val) { buffer.add(&val, sizeof(uint16_t)); }
  void BufUint32Add(Buffer::Instance& buffer, uint32_t val) { buffer.add(&val, sizeof(uint32_t)); }
  void BufStringAdd(Buffer::Instance& buffer, const std::string& str) { buffer.add(str); }
  int DrainBytes(Buffer::Instance& buffer, int skip_bytes);
  int BufStringDrain(Buffer::Instance& buffer, std::string& str);
  int BufStringDrainBySize(Buffer::Instance& buffer, std::string& str, int len);
  std::string BufToString(Buffer::Instance& buffer);
  std::string EncodeHdr(const std::string& cmd_str, int seq);
  int HdrReadDrain(Buffer::Instance& buffer, int& len, int& seq);
  int GetSeq() { return seq_; }
  void SetSeq(int seq);
  bool EndOfBuffer(Buffer::Instance& buffer);

private:
  uint64_t offset_;
  int seq_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
