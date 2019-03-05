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
  case enumToIntSigned(XidCodes::CONNECT_XID):
    parseConnect(data, offset, len);
    return;
  case enumToIntSigned(XidCodes::PING_XID):
    // Skip opcode.
    offset += 4;
    callbacks_.onPing();
    return;
  case enumToIntSigned(XidCodes::AUTH_XID):
    parseAuthRequest(data, offset, len);
    return;
  default:
    break;
  }

  // "Regular" requests.
  int32_t opcode;
  BufferHelper::peekInt32(data, offset, opcode);
  switch (opcode) {
  case enumToInt(OpCodes::GETDATA):
    parseGetDataRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::CREATE):
    parseCreateRequest(data, offset, len, false);
    break;
  case enumToInt(OpCodes::CREATE2):
    parseCreateRequest(data, offset, len, true);
    break;
  case enumToInt(OpCodes::SETDATA):
    parseSetRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::GETCHILDREN):
    parseGetChildrenRequest(data, offset, len, false);
    break;
  case enumToInt(OpCodes::GETCHILDREN2):
    parseGetChildrenRequest(data, offset, len, true);
    break;
  case enumToInt(OpCodes::DELETE):
    parseDeleteRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::EXISTS):
    parseExistsRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::GETACL):
    parseGetAclRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::SETACL):
    parseSetAclRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::SYNC):
    parseSyncRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::CHECK):
    parseCheckRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::MULTI):
    parseMultiRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::RECONFIG):
    parseReconfigRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::SETWATCHES):
    parseSetWatchesRequest(data, offset, len);
    break;
  case enumToIntSigned(OpCodes::CLOSE):
    callbacks_.onCloseRequest();
    break;
  default:
    break;
  }
}

#define CHECK_LENGTH(LEN, MINL)                                                                    \
  if (LEN < MINL) {                                                                                \
    throw EnvoyException("Package is too small");                                                  \
  }

void DecoderImpl::parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 28);

  // Read password - skip zxid, timeout, and session id.
  std::string passwd;
  offset += 20;
  BufferHelper::peekString(data, offset, passwd);

  // Read readonly flag, if it's there.
  bool readonly{};
  if (data.length() >= offset + 1) {
    BufferHelper::peekBool(data, offset, readonly);
  }

  callbacks_.onConnect(readonly);
}

void DecoderImpl::parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 20);

  // Skip opcode + type.
  offset += 8;
  std::string scheme;
  BufferHelper::peekString(data, offset, scheme);
  std::string credential;
  BufferHelper::peekString(data, offset, credential);

  callbacks_.onAuthRequest(scheme);
}

void DecoderImpl::parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 13);

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
  CHECK_LENGTH(len, 20);

  std::string path;
  BufferHelper::peekString(data, offset, path);
  // Skip data.
  int32_t datalen;
  BufferHelper::peekInt32(data, offset, datalen);
  offset += datalen;
  skipAcls(data, offset);
  int32_t flags;
  BufferHelper::peekInt32(data, offset, flags);
  const bool ephemeral = (flags & 0x1) == 1;
  const bool sequence = (flags & 0x2) == 2;

  callbacks_.onCreateRequest(path, ephemeral, sequence, two);
}

void DecoderImpl::parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 20);

  std::string path;
  BufferHelper::peekString(data, offset, path);
  // Skip data.
  int32_t datalen;
  BufferHelper::peekInt32(data, offset, datalen);
  offset += datalen;
  int32_t version;
  BufferHelper::peekInt32(data, offset, version);

  callbacks_.onSetRequest(path);
}

void DecoderImpl::parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                          const bool two) {
  CHECK_LENGTH(len, 14);

  std::string path;
  BufferHelper::peekString(data, offset, path);
  bool watch;
  BufferHelper::peekBool(data, offset, watch);

  callbacks_.onGetChildrenRequest(path, watch, two);
}

void DecoderImpl::parseDeleteRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 16);

  std::string path;
  BufferHelper::peekString(data, offset, path);
  int32_t version;
  BufferHelper::peekInt32(data, offset, version);

  callbacks_.onDeleteRequest(path, version);
}

void DecoderImpl::parseExistsRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 13);

  std::string path;
  BufferHelper::peekString(data, offset, path);
  bool watch;
  BufferHelper::peekBool(data, offset, watch);

  callbacks_.onExistsRequest(path, watch);
}

void DecoderImpl::parseGetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 8);

  std::string path;
  BufferHelper::peekString(data, offset, path);

  callbacks_.onGetAclRequest(path);
}

void DecoderImpl::parseSetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 8);

  std::string path;
  BufferHelper::peekString(data, offset, path);
  skipAcls(data, offset);
  int32_t version;
  BufferHelper::peekInt32(data, offset, version);

  callbacks_.onSetAclRequest(path, version);
}

void DecoderImpl::parseSyncRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 8);

  std::string path;
  BufferHelper::peekString(data, offset, path);

  callbacks_.onSyncRequest(path);
}

void DecoderImpl::parseCheckRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 8);

  std::string path;
  BufferHelper::peekString(data, offset, path);
  int32_t version;
  BufferHelper::peekInt32(data, offset, version);

  callbacks_.onCheckRequest(path, version);
}

void DecoderImpl::parseMultiRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  // Treat empty transactions as a decoding error, there should be at least 1 header.
  CHECK_LENGTH(len, 17);

  while (true) {
    int32_t type;
    BufferHelper::peekInt32(data, offset, type);
    bool done{};
    BufferHelper::peekBool(data, offset, done);
    int32_t error;
    BufferHelper::peekInt32(data, offset, error);

    if (done) {
      break;
    }

    switch (type) {
    case enumToInt(OpCodes::CREATE):
      parseCreateRequest(data, offset, len, false);
      break;
    case enumToInt(OpCodes::SETDATA):
      parseSetRequest(data, offset, len);
      break;
    case enumToInt(OpCodes::CHECK):
      parseCheckRequest(data, offset, len);
      break;
    default:
      // Should not happen.
      throw EnvoyException("Unknown type within a transaction");
    }
  }

  callbacks_.onMultiRequest();
}

void DecoderImpl::parseReconfigRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 28);

  // Skip joining.
  int32_t datalen;
  BufferHelper::peekInt32(data, offset, datalen);
  offset += datalen;
  // Skip leaving.
  BufferHelper::peekInt32(data, offset, datalen);
  offset += datalen;
  // Skip new members.
  BufferHelper::peekInt32(data, offset, datalen);
  offset += datalen;
  // Read config id.
  int64_t config_id;
  BufferHelper::peekInt64(data, offset, config_id);

  callbacks_.onReconfigRequest();
}

void DecoderImpl::parseSetWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  CHECK_LENGTH(len, 12);

  // Data watches.
  skipStrings(data, offset);
  // Exist watches.
  skipStrings(data, offset);
  // Child watches.
  skipStrings(data, offset);

  callbacks_.onSetWatchesRequest();
}

void DecoderImpl::skipStrings(Buffer::Instance& data, uint64_t& offset) const {
  int32_t count;
  BufferHelper::peekInt32(data, offset, count);

  for (int i = 0; i < count; ++i) {
    int32_t len;
    BufferHelper::peekInt32(data, offset, len);
    offset += len;
  }
}

void DecoderImpl::onData(Buffer::Instance& data) {
  uint64_t offset = 0;
  while (offset < data.length()) {
    try {
      decode(data, offset);
    } catch (EnvoyException& e) {
      callbacks_.onDecodeError();
    }
  }
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
