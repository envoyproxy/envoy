#include "extensions/filters/network/zookeeper_proxy/zookeeper_decoder.h"

#include "extensions/filters/network/zookeeper_proxy/zookeeper_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

constexpr uint32_t BOOL_LENGTH = 1;
constexpr uint32_t INT_LENGTH = 4;
constexpr uint32_t LONG_LENGTH = 8;
constexpr uint32_t XID_LENGTH = 4;
constexpr uint32_t OPCODE_LENGTH = 4;
constexpr uint32_t ZXID_LENGTH = 8;
constexpr uint32_t TIMEOUT_LENGTH = 4;
constexpr uint32_t SESSION_LENGTH = 8;
constexpr uint32_t MULTI_HEADER_LENGTH = 9;

const char* createFlagsToString(CreateFlags flags) {
  switch (flags) {
  case CreateFlags::PERSISTENT:
    return "persistent";
  case CreateFlags::PERSISTENT_SEQUENTIAL:
    return "persistent_sequential";
  case CreateFlags::EPHEMERAL:
    return "ephemeral";
  case CreateFlags::EPHEMERAL_SEQUENTIAL:
    return "ephemeral_sequential";
  case CreateFlags::CONTAINER:
    return "container";
  case CreateFlags::PERSISTENT_WITH_TTL:
    return "persistent_with_ttl";
  case CreateFlags::PERSISTENT_SEQUENTIAL_WITH_TTL:
    return "persistent_sequential_with_ttl";
  }

  return "unknown";
}

void DecoderImpl::decode(Buffer::Instance& data, uint64_t& offset) {
  ENVOY_LOG(trace, "zookeeper_proxy: decoding {} bytes at offset {}", data.length(), offset);

  // Check message length.
  const int32_t len = BufferHelper::peekInt32(data, offset);
  ensureMinLength(len, INT_LENGTH + XID_LENGTH);
  ensureMaxLength(len);

  // Control requests, with XIDs <= 0.
  //
  // These are meant to control the state of a session:
  // connect, keep-alive, authenticate and set initial watches.
  //
  // Note: setWatches is a command historically used to set watches
  //       right after connecting, typically used when roaming from one
  //       ZooKeeper server to the next. Thus, the special xid.
  //       However, some client implementations might expose setWatches
  //       as a regular data request, so we support that as well.
  const int32_t xid = BufferHelper::peekInt32(data, offset);
  switch (xid) {
  case enumToIntSigned(XidCodes::CONNECT_XID):
    parseConnect(data, offset, len);
    return;
  case enumToIntSigned(XidCodes::PING_XID):
    offset += OPCODE_LENGTH;
    callbacks_.onPing();
    return;
  case enumToIntSigned(XidCodes::AUTH_XID):
    parseAuthRequest(data, offset, len);
    return;
  case enumToIntSigned(XidCodes::SET_WATCHES_XID):
    offset += OPCODE_LENGTH;
    parseSetWatchesRequest(data, offset, len);
    return;
  }

  // Data requests, with XIDs > 0.
  //
  // These are meant to happen after a successful control request, except
  // for two cases: auth requests can happen at any time and ping requests
  // must happen every 1/3 of the negotiated session timeout, to keep
  // the session alive.
  const int32_t opcode = BufferHelper::peekInt32(data, offset);
  switch (opcode) {
  case enumToInt(OpCodes::GETDATA):
    parseGetDataRequest(data, offset, len);
    break;
  case enumToInt(OpCodes::CREATE):
  case enumToInt(OpCodes::CREATE2):
  case enumToInt(OpCodes::CREATECONTAINER):
  case enumToInt(OpCodes::CREATETTL):
    parseCreateRequest(data, offset, len, static_cast<OpCodes>(opcode));
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
  case enumToInt(OpCodes::CHECKWATCHES):
    parseXWatchesRequest(data, offset, len, OpCodes::CHECKWATCHES);
    break;
  case enumToInt(OpCodes::REMOVEWATCHES):
    parseXWatchesRequest(data, offset, len, OpCodes::REMOVEWATCHES);
    break;
  case enumToIntSigned(OpCodes::CLOSE):
    callbacks_.onCloseRequest();
    break;
  default:
    throw EnvoyException(fmt::format("Unknown opcode: {}", opcode));
  }
}

void DecoderImpl::ensureMinLength(const int32_t len, const int32_t minlen) const {
  if (len < minlen) {
    throw EnvoyException("Packet is too small");
  }
}

void DecoderImpl::ensureMaxLength(const int32_t len) const {
  if (static_cast<uint32_t>(len) > max_packet_bytes_) {
    throw EnvoyException("Packet is too big");
  }
}

void DecoderImpl::parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + ZXID_LENGTH + TIMEOUT_LENGTH + SESSION_LENGTH + INT_LENGTH);

  // Skip zxid, timeout, and session id.
  offset += ZXID_LENGTH + TIMEOUT_LENGTH + SESSION_LENGTH;

  // Skip password.
  skipString(data, offset);

  // Read readonly flag, if it's there.
  bool readonly{};
  if (data.length() >= offset + 1) {
    readonly = BufferHelper::peekBool(data, offset);
  }

  callbacks_.onConnect(readonly);
}

void DecoderImpl::parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + INT_LENGTH + INT_LENGTH);

  // Skip opcode + type.
  offset += OPCODE_LENGTH + INT_LENGTH;
  const std::string scheme = BufferHelper::peekString(data, offset);
  // Skip credential.
  skipString(data, offset);

  callbacks_.onAuthRequest(scheme);
}

void DecoderImpl::parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + BOOL_LENGTH);

  const std::string path = BufferHelper::peekString(data, offset);
  const bool watch = BufferHelper::peekBool(data, offset);

  callbacks_.onGetDataRequest(path, watch);
}

void DecoderImpl::skipAcls(Buffer::Instance& data, uint64_t& offset) const {
  const int32_t count = BufferHelper::peekInt32(data, offset);

  for (int i = 0; i < count; ++i) {
    // Perms.
    BufferHelper::peekInt32(data, offset);
    // Skip scheme.
    skipString(data, offset);
    // Skip cred.
    skipString(data, offset);
  }
}

void DecoderImpl::parseCreateRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                     OpCodes opcode) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH));

  const std::string path = BufferHelper::peekString(data, offset);

  // Skip data.
  skipString(data, offset);
  skipAcls(data, offset);

  CreateFlags flags = CreateFlags::PERSISTENT;
  switch (BufferHelper::peekInt32(data, offset)) {
  case 6:
    flags = CreateFlags::PERSISTENT_SEQUENTIAL_WITH_TTL;
    break;
  case 5:
    flags = CreateFlags::PERSISTENT_WITH_TTL;
    break;
  case 4:
    flags = CreateFlags::CONTAINER;
    break;
  case 3:
    flags = CreateFlags::EPHEMERAL_SEQUENTIAL;
    break;
  case 2:
    flags = CreateFlags::PERSISTENT_SEQUENTIAL;
    break;
  case 1:
    flags = CreateFlags::EPHEMERAL;
    break;
  }

  callbacks_.onCreateRequest(path, flags, opcode);
}

void DecoderImpl::parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH));

  const std::string path = BufferHelper::peekString(data, offset);
  // Skip data.
  skipString(data, offset);
  // Ignore version.
  BufferHelper::peekInt32(data, offset);

  callbacks_.onSetRequest(path);
}

void DecoderImpl::parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                          const bool two) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + BOOL_LENGTH);

  const std::string path = BufferHelper::peekString(data, offset);
  const bool watch = BufferHelper::peekBool(data, offset);

  callbacks_.onGetChildrenRequest(path, watch, two);
}

void DecoderImpl::parseDeleteRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (2 * INT_LENGTH));

  const std::string path = BufferHelper::peekString(data, offset);
  const int32_t version = BufferHelper::peekInt32(data, offset);

  callbacks_.onDeleteRequest(path, version);
}

void DecoderImpl::parseExistsRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + BOOL_LENGTH);

  const std::string path = BufferHelper::peekString(data, offset);
  const bool watch = BufferHelper::peekBool(data, offset);

  callbacks_.onExistsRequest(path, watch);
}

void DecoderImpl::parseGetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH);

  const std::string path = BufferHelper::peekString(data, offset);

  callbacks_.onGetAclRequest(path);
}

void DecoderImpl::parseSetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (2 * INT_LENGTH));

  const std::string path = BufferHelper::peekString(data, offset);
  skipAcls(data, offset);
  const int32_t version = BufferHelper::peekInt32(data, offset);

  callbacks_.onSetAclRequest(path, version);
}

void DecoderImpl::parseSyncRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH);

  const std::string path = BufferHelper::peekString(data, offset);

  callbacks_.onSyncRequest(path);
}

void DecoderImpl::parseCheckRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, (2 * INT_LENGTH));

  const std::string path = BufferHelper::peekString(data, offset);
  const int32_t version = BufferHelper::peekInt32(data, offset);

  callbacks_.onCheckRequest(path, version);
}

void DecoderImpl::parseMultiRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  // Treat empty transactions as a decoding error, there should be at least 1 header.
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + MULTI_HEADER_LENGTH);

  while (true) {
    const int32_t type = BufferHelper::peekInt32(data, offset);
    const bool done = BufferHelper::peekBool(data, offset);
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
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH) + LONG_LENGTH);

  // Skip joining.
  skipString(data, offset);
  // Skip leaving.
  skipString(data, offset);
  // Skip new members.
  skipString(data, offset);
  // Read config id.
  BufferHelper::peekInt64(data, offset);

  callbacks_.onReconfigRequest();
}

void DecoderImpl::parseSetWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH));

  // Data watches.
  skipStrings(data, offset);
  // Exist watches.
  skipStrings(data, offset);
  // Child watches.
  skipStrings(data, offset);

  callbacks_.onSetWatchesRequest();
}

void DecoderImpl::parseXWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                       OpCodes opcode) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (2 * INT_LENGTH));

  const std::string path = BufferHelper::peekString(data, offset);
  const int32_t type = BufferHelper::peekInt32(data, offset);

  if (opcode == OpCodes::CHECKWATCHES) {
    callbacks_.onCheckWatchesRequest(path, type);
  } else {
    callbacks_.onRemoveWatchesRequest(path, type);
  }
}

void DecoderImpl::skipString(Buffer::Instance& data, uint64_t& offset) const {
  const int32_t slen = BufferHelper::peekInt32(data, offset);
  offset += slen;
}

void DecoderImpl::skipStrings(Buffer::Instance& data, uint64_t& offset) const {
  const int32_t count = BufferHelper::peekInt32(data, offset);

  for (int i = 0; i < count; ++i) {
    skipString(data, offset);
  }
}

void DecoderImpl::onData(Buffer::Instance& data) {
  uint64_t offset = 0;
  try {
    while (offset < data.length()) {
      const uint64_t current = offset;
      decode(data, offset);
      callbacks_.onRequestBytes(offset - current);
    }
  } catch (const EnvoyException& e) {
    ENVOY_LOG(debug, "zookeeper_proxy: decoding exception {}", e.what());
    callbacks_.onDecodeError();
  }
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
