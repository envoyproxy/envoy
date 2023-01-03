#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

bool BufferHelper::endOfBuffer(Buffer::Instance& buffer) { return buffer.length() == 0; }

DecodeStatus BufferHelper::skipBytes(Buffer::Instance& buffer, size_t skip_bytes) {
  if (buffer.length() < skip_bytes) {
    return DecodeStatus::Failure;
  }
  buffer.drain(skip_bytes);
  return DecodeStatus::Success;
}
DecodeStatus BufferHelper::readUint8(Buffer::Instance& buffer, uint8_t& val) {
  try {
    val = buffer.peekLEInt<uint8_t>(0);
    buffer.drain(sizeof(uint8_t));
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::readUint16(Buffer::Instance& buffer, uint16_t& val) {
  try {
    val = buffer.peekLEInt<uint16_t>(0);
    buffer.drain(sizeof(uint16_t));
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::readUint24(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t, sizeof(uint8_t) * 3>(0);
    // val = buffer.peekBEInt<uint32_t, sizeof(uint8_t) * 3>(0);
    std::cout << "response code 3 bytes: " << val << "\n";
    buffer.drain(sizeof(uint8_t) * 3);
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::readUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    buffer.drain(sizeof(uint32_t));
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::readStringBySize(Buffer::Instance& buffer, size_t len,
                                            std::string& str) {
  if (buffer.length() < len) {
    return DecodeStatus::Failure;
  }
  str.assign(static_cast<char*>(buffer.linearize(len)), len);
  buffer.drain(len);
  return DecodeStatus::Success;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy