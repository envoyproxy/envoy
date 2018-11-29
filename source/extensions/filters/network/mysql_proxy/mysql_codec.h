#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_session.h"

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

  PACKED_STRUCT(struct header_fields {
    uint32_t length_ : 24;
    uint32_t seq_ : 8;
  });

  union MySQLHeader {
    header_fields fields_;
    uint32_t bits_;
  };

  virtual ~MySQLCodec(){};

  virtual int decode(Buffer::Instance& data, uint64_t& offset, int seq, int len) PURE;
  virtual std::string encode() PURE;

  int getSeq() { return seq_; }
  void setSeq(int seq) { seq_ = seq; }

private:
  int seq_;
};

/**
 * IO helpers for reading/writing MySQL data from/to a buffer.
 */
class BufferHelper : public Logger::Loggable<Logger::Id::filter> {
public:
  static void addUint8(Buffer::Instance& buffer, uint8_t val);
  static void addUint16(Buffer::Instance& buffer, uint16_t val);
  static void addUint32(Buffer::Instance& buffer, uint32_t val);
  static void addString(Buffer::Instance& buffer, const std::string& str);
  static std::string toString(Buffer::Instance& buffer);
  static std::string encodeHdr(const std::string& cmd_str, int seq);
  static bool endOfBuffer(Buffer::Instance& buffer, uint64_t& offset);
  static int peekUint8(Buffer::Instance& buffer, uint64_t& offset, uint8_t& val);
  static int peekUint16(Buffer::Instance& buffer, uint64_t& offset, uint16_t& val);
  static int peekUint32(Buffer::Instance& buffer, uint64_t& offset, uint32_t& val);
  static int peekUint64(Buffer::Instance& buffer, uint64_t& offset, uint64_t& val);
  static int peekBySize(Buffer::Instance& buffer, uint64_t& offset, int len, int& val);
  static int peekLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& offset, int& val);
  static int peekBytes(Buffer::Instance& buffer, uint64_t& offset, int skip_bytes);
  static int peekString(Buffer::Instance& buffer, uint64_t& offset, std::string& str);
  static int peekStringBySize(Buffer::Instance& buffer, uint64_t& offset, int len,
                              std::string& str);
  static int peekHdr(Buffer::Instance& buffer, uint64_t& offset, int& len, int& seq);
};

/**
 * General callbacks for dispatching decoded MySQL messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() {}

  virtual void decode(Buffer::Instance& message, uint64_t& offset, int seq, int len) PURE;
  virtual void onProtocolError() PURE;
  virtual void onNewMessage(MySQLSession::State state) PURE;
};

/**
 * MySQL message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() {}

  virtual void onData(Buffer::Instance& data) PURE;
};

typedef std::unique_ptr<Decoder> DecoderPtr;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks& callbacks, MySQLSession& session)
      : callbacks_(callbacks), session_(session) {}

  // MySQLProxy::Decoder
  void onData(Buffer::Instance& data) override;

private:
  bool decode(Buffer::Instance& data, uint64_t& offset);

  DecoderCallbacks& callbacks_;
  MySQLSession& session_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
