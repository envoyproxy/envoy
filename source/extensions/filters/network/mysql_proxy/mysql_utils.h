#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"
#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * IO helpers for reading/writing MySQL data from/to a buffer.
 * MySQL uses unsigned integer values in Little Endian format only.
 */
class BufferHelper : public Logger::Loggable<Logger::Id::filter> {
public:
  static void addUint8(Buffer::Instance& buffer, uint8_t val);
  static void addUint16(Buffer::Instance& buffer, uint16_t val);
  static void addUint24(Buffer::Instance& buffer, uint32_t val);
  static void addUint32(Buffer::Instance& buffer, uint32_t val);
  static void addLengthEncodedInteger(Buffer::Instance& buffer, uint64_t val);
  static void addString(Buffer::Instance& buffer, const std::string& str);
  static void encodeHdr(Buffer::Instance& pkg, uint8_t seq);
  static bool endOfBuffer(Buffer::Instance& buffer);
  static DecodeStatus readUint8(Buffer::Instance& buffer, uint8_t& val);
  static DecodeStatus readUint16(Buffer::Instance& buffer, uint16_t& val);
  static DecodeStatus readUint24(Buffer::Instance& buffer, uint32_t& val);
  static DecodeStatus readUint32(Buffer::Instance& buffer, uint32_t& val);
  static DecodeStatus readLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& val);
  static DecodeStatus readBytes(Buffer::Instance& buffer, size_t skip_bytes);
  static DecodeStatus readString(Buffer::Instance& buffer, std::string& str);
  static DecodeStatus readStringBySize(Buffer::Instance& buffer, size_t len, std::string& str);
  static DecodeStatus readStringEof(Buffer::Instance& buffer, std::string& str);
  static DecodeStatus readAll(Buffer::Instance& buffer, std::string& str);
  static DecodeStatus peekUint32(Buffer::Instance& buffer, uint32_t& val);
  static DecodeStatus peekUint16(Buffer::Instance& buffer, uint16_t& val);
  static void consumeHdr(Buffer::Instance& buffer);
  static DecodeStatus peekHdr(Buffer::Instance& buffer, uint32_t& len, uint8_t& seq);
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
