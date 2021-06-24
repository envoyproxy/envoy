#include "source/extensions/filters/network/mysql_proxy/mysql_utils.h"

#include <openssl/digest.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void BufferHelper::addUint8(Buffer::Instance& buffer, uint8_t val) {
  buffer.writeLEInt<uint8_t>(val);
}

void BufferHelper::addUint16(Buffer::Instance& buffer, uint16_t val) {
  buffer.writeLEInt<uint16_t>(val);
}

void BufferHelper::addUint24(Buffer::Instance& buffer, uint32_t val) {
  buffer.writeLEInt<uint32_t, sizeof(uint8_t) * 3>(val);
}

void BufferHelper::addUint32(Buffer::Instance& buffer, uint32_t val) {
  buffer.writeLEInt<uint32_t>(val);
}

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::FixedLengthInteger
void BufferHelper::addLengthEncodedInteger(Buffer::Instance& buffer, uint64_t val) {
  if (val < 251) {
    buffer.writeLEInt<uint8_t>(val);
  } else if (val < (1 << 16)) {
    buffer.writeLEInt<uint8_t>(0xfc);
    buffer.writeLEInt<uint16_t>(val);
  } else if (val < (1 << 24)) {
    buffer.writeLEInt<uint8_t>(0xfd);
    buffer.writeLEInt<uint64_t, sizeof(uint8_t) * 3>(val);
  } else {
    buffer.writeLEInt<uint8_t>(0xfe);
    buffer.writeLEInt<uint64_t>(val);
  }
}

void BufferHelper::addBytes(Buffer::Instance& buffer, const char* str, int size) {
  buffer.add(str, size);
}

void BufferHelper::encodeHdr(Buffer::Instance& pkg, uint8_t seq) {
  // the pkg buffer should only contain one package data
  uint32_t header = (seq << 24) | (pkg.length() & MYSQL_HDR_PKT_SIZE_MASK);
  Buffer::OwnedImpl buffer;
  addUint32(buffer, header);
  pkg.prepend(buffer);
}

bool BufferHelper::endOfBuffer(Buffer::Instance& buffer) { return buffer.length() == 0; }

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

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::LengthEncodedInteger
DecodeStatus BufferHelper::readLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& val) {
  uint8_t byte_val = 0;
  if (readUint8(buffer, byte_val) == DecodeStatus::Failure) {
    return DecodeStatus::Failure;
  }
  if (byte_val < LENENCODINT_1BYTE) {
    val = byte_val;
    return DecodeStatus::Success;
  }

  try {
    if (byte_val == LENENCODINT_2BYTES) {
      val = buffer.peekLEInt<uint64_t, sizeof(uint16_t)>(0);
      buffer.drain(sizeof(uint16_t));
    } else if (byte_val == LENENCODINT_3BYTES) {
      val = buffer.peekLEInt<uint64_t, sizeof(uint8_t) * 3>(0);
      buffer.drain(sizeof(uint8_t) * 3);
    } else if (byte_val == LENENCODINT_8BYTES) {
      val = buffer.peekLEInt<uint64_t>(0);
      buffer.drain(sizeof(uint64_t));
    } else {
      return DecodeStatus::Failure;
    }
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }

  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::skipBytes(Buffer::Instance& buffer, size_t skip_bytes) {
  if (buffer.length() < skip_bytes) {
    return DecodeStatus::Failure;
  } // namespace MySQLProxy
  buffer.drain(skip_bytes);
  return DecodeStatus::Success;
} // namespace NetworkFilters

DecodeStatus BufferHelper::readString(Buffer::Instance& buffer, std::string& str) {
  char end = MYSQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), 0);
  if (index == -1) {
    return DecodeStatus::Failure;
  }
  str.assign(static_cast<char*>(buffer.linearize(index)), index);
  buffer.drain(index + 1);
  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::readVector(Buffer::Instance& buffer, std::vector<uint8_t>& str) {
  char end = MYSQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), 0);
  if (index == -1) {
    return DecodeStatus::Failure;
  }
  auto arr = reinterpret_cast<uint8_t*>(buffer.linearize(index));
  str.assign(arr, arr + index);
  buffer.drain(index + 1);
  return DecodeStatus::Success;
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

DecodeStatus BufferHelper::readVectorBySize(Buffer::Instance& buffer, size_t len,
                                            std::vector<uint8_t>& data) {
  if (buffer.length() < len) {
    return DecodeStatus::Failure;
  }
  uint8_t* arr = reinterpret_cast<uint8_t*>(buffer.linearize(len));
  data.assign(&arr[0], &arr[len]);
  buffer.drain(len);
  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::peekUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::peekUint8(Buffer::Instance& buffer, uint8_t& val) {
  try {
    val = buffer.peekLEInt<uint8_t>(0);
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    return DecodeStatus::Failure;
  }
}

void BufferHelper::consumeHdr(Buffer::Instance& buffer) { buffer.drain(sizeof(uint32_t)); }

DecodeStatus BufferHelper::peekHdr(Buffer::Instance& buffer, uint32_t& len, uint8_t& seq) {
  uint32_t val = 0;
  if (peekUint32(buffer, val) != DecodeStatus::Success) {
    return DecodeStatus::Failure;
  }
  seq = htobe32(val) & MYSQL_HDR_SEQ_MASK;
  len = val & MYSQL_HDR_PKT_SIZE_MASK;
  ENVOY_LOG(trace, "mysql_proxy: MYSQL-hdrseq {}, len {}", seq, len);
  return DecodeStatus::Success;
}

AuthMethod AuthHelper::authMethod(uint32_t cap, const std::string& auth_plugin_name) {

  if (auth_plugin_name.empty()) {
    /*
     * https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase.html
     */
    bool v41 = cap & CLIENT_PROTOCOL_41;
    bool sconn = cap & CLIENT_SECURE_CONNECTION;
    if (!v41 || !sconn) {
      return AuthMethod::OldPassword;
    }
    return AuthMethod::NativePassword;
  }

  if (auth_plugin_name == "mysql_old_password") {
    return AuthMethod::OldPassword;
  }
  if (auth_plugin_name == "mysql_native_password") {
    return AuthMethod::NativePassword;
  }
  if (auth_plugin_name == "sha256_password") {
    return AuthMethod::Sha256Password;
  }
  if (auth_plugin_name == "caching_sha2_password") {
    return AuthMethod::CacheSha2Password;
  }
  if (auth_plugin_name == "mysql_clear_password") {
    return AuthMethod::ClearPassword;
  }
  return AuthMethod::Unknown;
}

// https://github.com/mysql/mysql-server/blob/5.5/sql/password.c#L186
std::vector<uint8_t> OldPassword::signature(const std::string& password,
                                            const std::vector<uint8_t>& seed) {
  std::vector<uint8_t> to;
  if (password.empty()) {
    return to;
  }
  to.resize(SEED_LENGTH);
  auto hash_pass = hash(password);
  auto hash_seed = hash(seed);
  RandStruct rand_st(hash_pass[0] ^ hash_seed[0], hash_pass[1] ^ hash_seed[1]);
  for (int i = 0; i < SEED_LENGTH; i++) {
    to[i] = static_cast<char>((floor(rand_st.myRnd() * 31) + 64));
  }
  char extra = static_cast<char>(floor(rand_st.myRnd() * 31));
  for (int i = 0; i < SEED_LENGTH; i++) {
    to[i] ^= extra;
  }
  return to;
}

std::vector<uint8_t> NativePassword::generateSeed() {
  std::vector<uint8_t> res(SEED_LENGTH);
  RAND_bytes(res.data(), SEED_LENGTH);
  return res;
}

std::vector<uint8_t> NativePassword::signature(const std::string& password,
                                               const std::vector<uint8_t>& seed) {
  auto hashstage1 = hash(password);
  auto hashstage2 = hash(hashstage1);

  auto text = seed;
  text.insert(text.end(), hashstage2.begin(), hashstage2.end());
  auto to_be_xor = hash(text);

  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    to_be_xor[i] = to_be_xor[i] ^ hashstage1[i];
  }
  return to_be_xor;
}

std::vector<uint8_t> NativePassword::hash(const char* text, int size) {
  bssl::ScopedEVP_MD_CTX ctx;
  std::vector<uint8_t> result(SHA_DIGEST_LENGTH);
  auto rc = EVP_DigestInit(ctx.get(), EVP_sha1());
  RELEASE_ASSERT(rc == 1, "Failed to init digest context");
  rc = EVP_DigestUpdate(ctx.get(), text, size);
  RELEASE_ASSERT(rc == 1, "Failed to update digest");
  rc = EVP_DigestFinal(ctx.get(), result.data(), nullptr);
  RELEASE_ASSERT(rc == 1, "Failed to finalize digest");
  return result;
}

std::vector<uint8_t> OldPassword::generateSeed() {
  std::vector<uint8_t> res(SEED_LENGTH);
  RAND_bytes(res.data(), SEED_LENGTH);
  return res;
}

std::vector<uint32_t> OldPassword::hash(const char* text, int size) {
  uint32_t nr = 1345345333L, add = 7, nr2 = 0x12345671L;
  uint32_t tmp;
  std::vector<uint32_t> result(2);
  for (int i = 0; i < size; i++) {
    if (text[i] == ' ' || text[i] == '\t') {
      continue;
    }
    tmp = static_cast<uint32_t>(text[i]);
    nr ^= (((nr & 63) + add) * tmp) + (nr << 8);
    nr2 += (nr2 << 8) ^ nr;
    add += tmp;
  }

  result[0] = nr & ((static_cast<uint32_t>(1L) << 31) - 1L); /* Don't use sign bit (str2int) */
  result[1] = nr2 & ((static_cast<uint32_t>(1L) << 31) - 1L);
  return result;
}

OldPassword::RandStruct::RandStruct(uint32_t seed1, uint32_t seed2) {
  max_value_ = 0x3FFFFFFFL;
  max_value_dbl_ = static_cast<double>(max_value_);
  seed1_ = seed1 % max_value_;
  seed2_ = seed2 % max_value_;
}

double OldPassword::RandStruct::myRnd() {
  seed1_ = (seed1_ * 3 + seed2_) % max_value_;
  seed2_ = (seed1_ + seed2_ + 33) % max_value_;
  return ((static_cast<double>(seed1_)) / max_value_dbl_);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
