#include "extensions/filters/network/zookeeper_proxy/zookeeper_decoder.h"

#include "extensions/filters/network/zookeeper_proxy/zookeeper_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

void DecoderImpl::decode(Buffer::Instance& data, uint64_t& offset) {
  ENVOY_LOG(trace, "zookeeper_proxy: decoding {} bytes at offset {}", data.length(), offset);

  // Check message length.
  int32_t len = BufferHelper::peekInt32(data, offset);
  checkLength(len, 8);

  // "Special" requests.
  int32_t xid = BufferHelper::peekInt32(data, offset);
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
  }

  // "Regular" requests.
  int32_t opcode = BufferHelper::peekInt32(data, offset);
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
    throw EnvoyException(fmt::format("Unknown opcode: {}", opcode));
  }
}

void DecoderImpl::checkLength(const int32_t len, const int32_t minlen) const {
  if (len < minlen) {
    throw EnvoyException("Package is too small");
  }
}

void DecoderImpl::parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 28);

  // Read password - skip zxid, timeout, and session id.
  offset += 20;
  std::string passwd = BufferHelper::peekString(data, offset);

  // Read readonly flag, if it's there.
  bool readonly{};
  if (data.length() >= offset + 1) {
    readonly = BufferHelper::peekBool(data, offset);
  }

  callbacks_.onConnect(readonly);
}

void DecoderImpl::parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 20);

  // Skip opcode + type.
  offset += 8;
  std::string scheme = BufferHelper::peekString(data, offset);
  std::string credential = BufferHelper::peekString(data, offset);

  callbacks_.onAuthRequest(scheme);
}

void DecoderImpl::parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 13);

  std::string path = BufferHelper::peekString(data, offset);
  bool watch = BufferHelper::peekBool(data, offset);

  callbacks_.onGetDataRequest(path, watch);
}

void DecoderImpl::skipAcls(Buffer::Instance& data, uint64_t& offset) const {
  int32_t count = BufferHelper::peekInt32(data, offset);

  for (int i = 0; i < count; ++i) {
    BufferHelper::peekInt32(data, offset);
    BufferHelper::peekString(data, offset);
    BufferHelper::peekString(data, offset);
  }
}

void DecoderImpl::parseCreateRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                     const bool two) {
  checkLength(len, 20);

  std::string path = BufferHelper::peekString(data, offset);

  // Skip data.
  int32_t datalen = BufferHelper::peekInt32(data, offset);
  offset += datalen;
  skipAcls(data, offset);
  int32_t flags = BufferHelper::peekInt32(data, offset);
  const bool ephemeral = (flags & 0x1) == 1;
  const bool sequence = (flags & 0x2) == 2;

  callbacks_.onCreateRequest(path, ephemeral, sequence, two);
}

void DecoderImpl::parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 20);

  std::string path = BufferHelper::peekString(data, offset);
  // Skip data.
  int32_t datalen = BufferHelper::peekInt32(data, offset);
  offset += datalen;
  // Ignore version.
  BufferHelper::peekInt32(data, offset);

  callbacks_.onSetRequest(path);
}

void DecoderImpl::parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                          const bool two) {
  checkLength(len, 14);

  std::string path = BufferHelper::peekString(data, offset);
  bool watch = BufferHelper::peekBool(data, offset);

  callbacks_.onGetChildrenRequest(path, watch, two);
}

void DecoderImpl::parseDeleteRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 16);

  std::string path = BufferHelper::peekString(data, offset);
  int32_t version = BufferHelper::peekInt32(data, offset);

  callbacks_.onDeleteRequest(path, version);
}

void DecoderImpl::parseExistsRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 13);

  std::string path = BufferHelper::peekString(data, offset);
  bool watch = BufferHelper::peekBool(data, offset);

  callbacks_.onExistsRequest(path, watch);
}

void DecoderImpl::parseGetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 8);

  std::string path = BufferHelper::peekString(data, offset);

  callbacks_.onGetAclRequest(path);
}

void DecoderImpl::parseSetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 8);

  std::string path = BufferHelper::peekString(data, offset);
  skipAcls(data, offset);
  int32_t version = BufferHelper::peekInt32(data, offset);

  callbacks_.onSetAclRequest(path, version);
}

void DecoderImpl::parseSyncRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 8);

  std::string path = BufferHelper::peekString(data, offset);

  callbacks_.onSyncRequest(path);
}

void DecoderImpl::parseCheckRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 8);

  std::string path = BufferHelper::peekString(data, offset);
  int32_t version = BufferHelper::peekInt32(data, offset);

  callbacks_.onCheckRequest(path, version);
}

void DecoderImpl::parseMultiRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  // Treat empty transactions as a decoding error, there should be at least 1 header.
  checkLength(len, 17);

  while (true) {
    int32_t type = BufferHelper::peekInt32(data, offset);
    bool done = BufferHelper::peekBool(data, offset);
    // Ignore error field.
    BufferHelper::peekInt32(data, offset);

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
  checkLength(len, 28);

  // Skip joining.
  int32_t datalen = BufferHelper::peekInt32(data, offset);
  offset += datalen;
  // Skip leaving.
  datalen = BufferHelper::peekInt32(data, offset);
  offset += datalen;
  // Skip new members.
  datalen = BufferHelper::peekInt32(data, offset);
  offset += datalen;
  // Read config id.
  BufferHelper::peekInt64(data, offset);

  callbacks_.onReconfigRequest();
}

void DecoderImpl::parseSetWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  checkLength(len, 12);

  // Data watches.
  skipStrings(data, offset);
  // Exist watches.
  skipStrings(data, offset);
  // Child watches.
  skipStrings(data, offset);

  callbacks_.onSetWatchesRequest();
}

void DecoderImpl::skipStrings(Buffer::Instance& data, uint64_t& offset) const {
  int32_t count = BufferHelper::peekInt32(data, offset);

  for (int i = 0; i < count; ++i) {
    int32_t len = BufferHelper::peekInt32(data, offset);
    offset += len;
  }
}

void DecoderImpl::onData(Buffer::Instance& data) {
  uint64_t offset = 0;
  try {
    while (offset < data.length()) {
      uint64_t current = offset;
      decode(data, offset);
      callbacks_.onRequestBytes(offset - current);
    }
  } catch (EnvoyException& e) {
    ENVOY_LOG(debug, "zookeeper_proxy: decoding exception {}", e.what());
    callbacks_.onDecodeError();
  }
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
