#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/zookeeper_proxy/utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

enum class XidCodes {
  CONNECT_XID = 0,
  WATCH_XID = -1,
  PING_XID = -2,
  AUTH_XID = -4,
  SET_WATCHES_XID = -8
};

enum class OpCodes {
  CONNECT = 0,
  CREATE = 1,
  DELETE = 2,
  EXISTS = 3,
  GETDATA = 4,
  SETDATA = 5,
  GETACL = 6,
  SETACL = 7,
  GETCHILDREN = 8,
  SYNC = 9,
  PING = 11,
  GETCHILDREN2 = 12,
  CHECK = 13,
  MULTI = 14,
  CREATE2 = 15,
  RECONFIG = 16,
  CHECKWATCHES = 17,
  REMOVEWATCHES = 18,
  CREATECONTAINER = 19,
  CREATETTL = 21,
  CLOSE = -11,
  SETAUTH = 100,
  SETWATCHES = 101,
  GETEPHEMERALS = 103,
  GETALLCHILDRENNUMBER = 104
};

enum class WatcherType { CHILDREN = 1, DATA = 2, ANY = 3 };

enum class CreateFlags {
  PERSISTENT,
  PERSISTENT_SEQUENTIAL,
  EPHEMERAL,
  EPHEMERAL_SEQUENTIAL,
  CONTAINER,
  PERSISTENT_WITH_TTL,
  PERSISTENT_SEQUENTIAL_WITH_TTL
};

const char* createFlagsToString(CreateFlags flags);

/**
 * General callbacks for dispatching decoded ZooKeeper messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void onDecodeError() PURE;
  virtual void onRequestBytes(uint64_t bytes) PURE;
  virtual void onConnect(bool readonly) PURE;
  virtual void onPing() PURE;
  virtual void onAuthRequest(const std::string& scheme) PURE;
  virtual void onGetDataRequest(const std::string& path, bool watch) PURE;
  virtual void onCreateRequest(const std::string& path, CreateFlags flags, OpCodes opcode) PURE;
  virtual void onSetRequest(const std::string& path) PURE;
  virtual void onGetChildrenRequest(const std::string& path, bool watch, bool v2) PURE;
  virtual void onGetEphemeralsRequest(const std::string& path) PURE;
  virtual void onGetAllChildrenNumberRequest(const std::string& path) PURE;
  virtual void onDeleteRequest(const std::string& path, int32_t version) PURE;
  virtual void onExistsRequest(const std::string& path, bool watch) PURE;
  virtual void onGetAclRequest(const std::string& path) PURE;
  virtual void onSetAclRequest(const std::string& path, int32_t version) PURE;
  virtual void onSyncRequest(const std::string& path) PURE;
  virtual void onCheckRequest(const std::string& path, int32_t version) PURE;
  virtual void onMultiRequest() PURE;
  virtual void onReconfigRequest() PURE;
  virtual void onSetWatchesRequest() PURE;
  virtual void onCheckWatchesRequest(const std::string& path, int32_t type) PURE;
  virtual void onRemoveWatchesRequest(const std::string& path, int32_t type) PURE;
  virtual void onCloseRequest() PURE;
};

/**
 * ZooKeeper message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual void onData(Buffer::Instance& data) PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  explicit DecoderImpl(DecoderCallbacks& callbacks, uint32_t max_packet_bytes)
      : callbacks_(callbacks), max_packet_bytes_(max_packet_bytes), helper_(max_packet_bytes) {}

  // ZooKeeperProxy::Decoder
  void onData(Buffer::Instance& data) override;

private:
  void decode(Buffer::Instance& data, uint64_t& offset);
  void parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseCreateRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len, OpCodes opcode);
  void skipAcls(Buffer::Instance& data, uint64_t& offset);
  void parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len, bool two);
  void parseDeleteRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseExistsRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseGetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseSetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseCheckRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseMultiRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseReconfigRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseSetWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  void parseXWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len, OpCodes opcode);
  void skipString(Buffer::Instance& data, uint64_t& offset);
  void skipStrings(Buffer::Instance& data, uint64_t& offset);
  void ensureMinLength(int32_t len, int32_t minlen) const;
  void ensureMaxLength(int32_t len) const;
  std::string pathOnlyRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);

  DecoderCallbacks& callbacks_;
  const uint32_t max_packet_bytes_;
  BufferHelper helper_;
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
