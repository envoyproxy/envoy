#include "extensions/filters/network/zookeeper_proxy/zookeeper_decoder.h"

#include "extensions/filters/network/zookeeper_proxy/zookeeper_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

void DecoderImpl::decode(Buffer::Instance& data, uint64_t& offset) {
  ENVOY_LOG(trace, "zookeeper_proxy: decoding {} bytes at offset {}", data.length(), offset);

  // Check message length.
  int32_t len;
  BufferHelper::peekInt32(data, offset, len);
  if (len < 8) {
    callbacks_.onDecodeError();
    return;
  }

  int32_t xid;
  BufferHelper::peekInt32(data, offset, xid);

  // "Special" requests.
  switch (xid) {
  case CONNECT_XID:
    parseConnect(data, offset, len);
    return;
  case PING_XID:
    callbacks_.onPing();
    return;
  case AUTH_XID:
    parseAuthRequest(data, offset, len);
    return;
  default:
    break;
  }

  // "Regular" requests.
  int32_t opcode;
  BufferHelper::peekInt32(data, offset, opcode);
  switch (opcode) {
  case enumToInt(Opcodes::GETDATA):
    parseGetDataRequest(data, offset, len);
    break;
  case enumToInt(Opcodes::CREATE):
    parseCreateRequest(data, offset, len, false);
    break;
  case enumToInt(Opcodes::CREATE2):
    parseCreateRequest(data, offset, len, true);
    break;
  case enumToInt(Opcodes::SETDATA):
    parseSetRequest(data, offset, len);
    break;
  case enumToInt(Opcodes::GETCHILDREN):
    parseGetChildrenRequest(data, offset, len, false);
    break;
  case enumToInt(Opcodes::GETCHILDREN2):
    parseGetChildrenRequest(data, offset, len, true);
    break;
  default:
    break;
  }
}

void DecoderImpl::parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  if (len < 28) {
    return;
  }

  // Read password - skip zxid, timeout, and session id.
  std::string passwd;
  offset += 20;
  BufferHelper::peekString(data, offset, passwd);

  // Read readonly flag, if it's there.
  bool readonly{};
  BufferHelper::peekBool(data, offset, readonly);

  callbacks_.onConnect(readonly);
}

void DecoderImpl::parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  if (len < 20) {
    return;
  }

  // Skip opcode & type.
  std::string scheme;
  offset += 8;
  BufferHelper::peekString(data, offset, scheme);
  std::string credential;
  BufferHelper::peekString(data, offset, credential);

  callbacks_.onAuthRequest(scheme);
}

void DecoderImpl::parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  if (len < 14) {
    return;
  }

  // Skip opcode.
  offset += 4;
  std::string path;
  BufferHelper::peekString(data, offset, path);
  bool watch;
  BufferHelper::peekBool(data, offset, watch);

  callbacks_.onGetDataRequest(path, watch);
}

void DecoderImpl::skipAcls(Buffer::Instance& data, uint64_t& offset) const {
  int32_t count;
  BufferHelper::peekInt32(data, offset, count);

  for (int i = 0; i < count; ++i) {
    int32_t perms;
    BufferHelper::peekInt32(data, offset, perms);
    std::string scheme;
    BufferHelper::peekString(data, offset, scheme);
    std::string credential;
    BufferHelper::peekString(data, offset, credential);
  }
}

void DecoderImpl::parseCreateRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                     const bool two) {
  if (len < 25) {
    return;
  }

  // Skip opcode.
  offset += 4;
  std::string path;
  BufferHelper::peekString(data, offset, path);
  // Skip data.
  int32_t datalen;
  BufferHelper::peekInt32(data, offset, datalen);
  offset += datalen;
  // Skip acls.
  skipAcls(data, offset);
  // Flags.
  int32_t flags;
  BufferHelper::peekInt32(data, offset, flags);
  const bool ephemeral = (flags & 0x1) == 1;
  const bool sequence = (flags & 0x2) == 2;

  callbacks_.onCreateRequest(path, ephemeral, sequence, two);
}

void DecoderImpl::parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  if (len < 20) {
    return;
  }

  // Skip opcode.
  offset += 4;
  std::string path;
  BufferHelper::peekString(data, offset, path);
  // Skip data.
  int32_t datalen;
  BufferHelper::peekInt32(data, offset, datalen);
  offset += datalen;
  // Version.
  int32_t version;
  BufferHelper::peekInt32(data, offset, version);

  callbacks_.onSetRequest(path);
}

void DecoderImpl::parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                          const bool two) {
  if (len < 14) {
    return;
  }

  // Skip opcode.
  offset += 4;
  std::string path;
  BufferHelper::peekString(data, offset, path);
  bool watch;
  BufferHelper::peekBool(data, offset, watch);

  callbacks_.onGetChildrenRequest(path, watch, two);
}

void DecoderImpl::onData(Buffer::Instance& data) {
  uint64_t offset = 0;
  while (offset < data.length()) {
    decode(data, offset);
  }
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
