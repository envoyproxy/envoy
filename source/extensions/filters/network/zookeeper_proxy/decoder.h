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
  ConnectXid = 0,
  WatchXid = -1,
  PingXid = -2,
  AuthXid = -4,
  SetWatchesXid = -8
};

enum class OpCodes {
  Connect = 0,
  Create = 1,
  Delete = 2,
  Exists = 3,
  GetData = 4,
  SetData = 5,
  GetAcl = 6,
  SetAcl = 7,
  GetChildren = 8,
  Sync = 9,
  Ping = 11,
  GetChildren2 = 12,
  Check = 13,
  Multi = 14,
  Create2 = 15,
  Reconfig = 16,
  CheckWatches = 17,
  RemoveWatches = 18,
  CreateContainer = 19,
  CreateTtl = 21,
  Close = -11,
  SetAuth = 100,
  SetWatches = 101,
  GetEphemerals = 103,
  GetAllChildrenNumber = 104
};

enum class WatcherType { Children = 1, Data = 2, Any = 3 };

enum class CreateFlags {
  Persistent,
  PersistentSequential,
  Ephemeral,
  EphemeralSequential,
  Container,
  PersistentWithTtl,
  PersistentSequentialWithTtl
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
  virtual void onResponseBytes(uint64_t bytes) PURE;
  virtual void onConnectResponse(int32_t proto_version, int32_t timeout, bool readonly,
                                 const std::chrono::milliseconds& latency) PURE;
  virtual void onResponse(OpCodes opcode, int32_t xid, int64_t zxid, int32_t error,
                          const std::chrono::milliseconds& latency) PURE;
  virtual void onWatchEvent(int32_t event_type, int32_t client_state, const std::string& path,
                            int64_t zxid, int32_t error) PURE;
};

/**
 * ZooKeeper message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual void onData(Buffer::Instance& data) PURE;
  virtual void onWrite(Buffer::Instance& data) PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  explicit DecoderImpl(DecoderCallbacks& callbacks, uint32_t max_packet_bytes,
                       TimeSource& time_source)
      : callbacks_(callbacks), max_packet_bytes_(max_packet_bytes), helper_(max_packet_bytes),
        time_source_(time_source) {}

  // ZooKeeperProxy::Decoder
  void onData(Buffer::Instance& data) override;
  void onWrite(Buffer::Instance& data) override;

private:
  enum class DecodeType { READ, WRITE };
  struct RequestBegin {
    OpCodes opcode;
    MonotonicTime start_time;
  };

  void decode(Buffer::Instance& data, DecodeType dtype);
  void decodeOnData(Buffer::Instance& data, uint64_t& offset);
  void decodeOnWrite(Buffer::Instance& data, uint64_t& offset);
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
  void parseConnectResponse(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                            const std::chrono::milliseconds& latency);
  void parseWatchEvent(Buffer::Instance& data, uint64_t& offset, uint32_t len, int64_t zxid,
                       int32_t error);
  bool maybeReadBool(Buffer::Instance& data, uint64_t& offset);

  DecoderCallbacks& callbacks_;
  const uint32_t max_packet_bytes_;
  BufferHelper helper_;
  TimeSource& time_source_;
  std::unordered_map<int32_t, RequestBegin> requests_by_xid_;
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
