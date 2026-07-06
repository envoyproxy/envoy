#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/byte_order.h"
#include "source/common/common/logger.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "openssl/crypto.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

// Secure memory buffer that is guaranteed to be zeroed on destruction
// via OPENSSL_cleanse, preventing password leakage in memory.
class SecureBytes {
public:
  explicit SecureBytes(size_t len) : data_(new uint8_t[len]), len_(len) {}

  ~SecureBytes() {
    if (data_ != nullptr) {
      OPENSSL_cleanse(data_, len_);
      delete[] data_;
    }
  }

  SecureBytes(const SecureBytes&) = delete;
  SecureBytes& operator=(const SecureBytes&) = delete;

  SecureBytes(SecureBytes&& other) noexcept : data_(other.data_), len_(other.len_) {
    other.data_ = nullptr;
    other.len_ = 0;
  }

  uint8_t* data() { return data_; }
  const uint8_t* data() const { return data_; }
  size_t size() const { return len_; }

  uint8_t operator[](size_t i) const { return data_[i]; }
  uint8_t& operator[](size_t i) { return data_[i]; }

private:
  uint8_t* data_{nullptr};
  size_t len_{0};
};

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
  static void addBytes(Buffer::Instance& buffer, const char* data, int size);
  static void addString(Buffer::Instance& buffer, const std::string& str) {
    addBytes(buffer, str.data(), str.size());
  }
  static void addVector(Buffer::Instance& buffer, const std::vector<uint8_t>& data) {
    addBytes(buffer, reinterpret_cast<const char*>(data.data()), data.size());
  }
  static void encodeHdr(Buffer::Instance& pkg, uint8_t seq);
  static bool endOfBuffer(Buffer::Instance& buffer);
  static DecodeStatus readUint8(Buffer::Instance& buffer, uint8_t& val);
  static DecodeStatus readUint16(Buffer::Instance& buffer, uint16_t& val);
  static DecodeStatus readUint24(Buffer::Instance& buffer, uint32_t& val);
  static DecodeStatus readUint32(Buffer::Instance& buffer, uint32_t& val);
  static DecodeStatus readLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& val);
  static DecodeStatus skipBytes(Buffer::Instance& buffer, size_t skip_bytes);
  static DecodeStatus readString(Buffer::Instance& buffer, std::string& str);
  static DecodeStatus readVector(Buffer::Instance& buffer, std::vector<uint8_t>& data);
  static DecodeStatus readStringBySize(Buffer::Instance& buffer, size_t len, std::string& str);
  static DecodeStatus readVectorBySize(Buffer::Instance& buffer, size_t len,
                                       std::vector<uint8_t>& vec);
  static DecodeStatus readAll(Buffer::Instance& buffer, std::string& str);
  static DecodeStatus peekUint32(Buffer::Instance& buffer, uint32_t& val);
  static DecodeStatus peekUint8(Buffer::Instance& buffer, uint8_t& val);
  static void consumeHdr(Buffer::Instance& buffer);
  static DecodeStatus peekHdr(Buffer::Instance& buffer, uint32_t& len, uint8_t& seq);

  // Read `len` bytes from buffer into a SecureBytes object backed by guarded memory,
  // then zero the original data in the buffer to prevent password leakage.
  static DecodeStatus readSecureBytes(Buffer::Instance& buffer, size_t len,
                                      std::unique_ptr<SecureBytes>& out);
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
