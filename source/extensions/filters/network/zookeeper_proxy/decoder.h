#pragma once

#include <cstdint>
#include <queue>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/network/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/network/zookeeper_proxy/utils.h"

#include "absl/container/node_hash_map.h"
#include "absl/status/statusor.h"

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
  GetAllChildrenNumber = 104,
  SetWatches2 = 105,
  AddWatch = 106,
};

enum class WatcherType { Children = 1, Data = 2, Any = 3 };

enum class AddWatchMode { Persistent, PersistentRecursive };

enum class CreateFlags {
  Persistent,
  Ephemeral,
  PersistentSequential,
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

  virtual void onDecodeError(const absl::optional<OpCodes> opcode) PURE;
  virtual void onRequestBytes(const absl::optional<OpCodes> opcode, const uint64_t bytes) PURE;
  virtual void onConnect(bool readonly) PURE;
  virtual void onPing() PURE;
  virtual void onAuthRequest(const std::string& scheme) PURE;
  virtual void onGetDataRequest(const std::string& path, bool watch) PURE;
  virtual absl::Status onCreateRequest(const std::string& path, CreateFlags flags,
                                       OpCodes opcode) PURE;
  virtual void onSetRequest(const std::string& path) PURE;
  virtual void onGetChildrenRequest(const std::string& path, bool watch, bool v2) PURE;
  virtual absl::Status onGetEphemeralsRequest(const absl::StatusOr<std::string>& path,
                                              const OpCodes opcode) PURE;
  virtual absl::Status onGetAllChildrenNumberRequest(const absl::StatusOr<std::string>& path,
                                                     const OpCodes opcode) PURE;
  virtual void onDeleteRequest(const std::string& path, int32_t version) PURE;
  virtual void onExistsRequest(const std::string& path, bool watch) PURE;
  virtual void onGetAclRequest(const std::string& path) PURE;
  virtual void onSetAclRequest(const std::string& path, int32_t version) PURE;
  virtual absl::Status onSyncRequest(const absl::StatusOr<std::string>& path,
                                     const OpCodes opcode) PURE;
  virtual void onCheckRequest(const std::string& path, int32_t version) PURE;
  virtual void onMultiRequest() PURE;
  virtual void onReconfigRequest() PURE;
  virtual void onSetWatchesRequest() PURE;
  virtual void onSetWatches2Request() PURE;
  virtual void onAddWatchRequest(const std::string& path, const int32_t mode) PURE;
  virtual void onCheckWatchesRequest(const std::string& path, int32_t type) PURE;
  virtual void onRemoveWatchesRequest(const std::string& path, int32_t type) PURE;
  virtual void onCloseRequest() PURE;
  virtual void onResponseBytes(const absl::optional<OpCodes> opcode, const uint64_t bytes) PURE;
  virtual void onConnectResponse(int32_t proto_version, int32_t timeout, bool readonly,
                                 const std::chrono::milliseconds latency) PURE;
  virtual void onResponse(OpCodes opcode, int32_t xid, int64_t zxid, int32_t error,
                          const std::chrono::milliseconds latency) PURE;
  virtual void onWatchEvent(int32_t event_type, int32_t client_state, const std::string& path,
                            int64_t zxid, int32_t error) PURE;
};

/**
 * ZooKeeper message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual Network::FilterStatus onData(Buffer::Instance& data) PURE;
  virtual Network::FilterStatus onWrite(Buffer::Instance& data) PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  explicit DecoderImpl(DecoderCallbacks& callbacks, uint32_t max_packet_bytes,
                       TimeSource& time_source)
      : callbacks_(callbacks), max_packet_bytes_(max_packet_bytes), helper_(max_packet_bytes),
        time_source_(time_source) {}

  // ZooKeeperProxy::Decoder
  Network::FilterStatus onData(Buffer::Instance& data) override;
  Network::FilterStatus onWrite(Buffer::Instance& data) override;

private:
  enum class DecodeType { READ, WRITE };
  struct RequestBegin {
    OpCodes opcode;
    MonotonicTime start_time;
  };

  // decodeAndBuffer
  // (1) prepends previous partial data to the current buffer,
  // (2) decodes all full packet(s),
  // (3) adds the rest of the data to the ZooKeeper filter buffer,
  // (4) removes the prepended data.
  Network::FilterStatus decodeAndBuffer(Buffer::Instance& data, DecodeType dtype,
                                        Buffer::OwnedImpl& zk_filter_buffer);
  absl::Status decodeAndBufferHelper(Buffer::Instance& data, DecodeType dtype,
                                     Buffer::OwnedImpl& zk_filter_buffer);
  void decode(Buffer::Instance& data, DecodeType dtype, uint64_t full_packets_len);
  // decodeOnData and decodeOnWrite return ZooKeeper opcode or absl::nullopt.
  // absl::nullopt indicates WATCH_XID, which is generated by the server and has no corresponding
  // opcode.
  absl::StatusOr<absl::optional<OpCodes>> decodeOnData(Buffer::Instance& data, uint64_t& offset);
  absl::StatusOr<absl::optional<OpCodes>> decodeOnWrite(Buffer::Instance& data, uint64_t& offset);
  absl::Status parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseCreateRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                  OpCodes opcode);
  absl::Status skipAcls(Buffer::Instance& data, uint64_t& offset);
  absl::Status parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                       bool two);
  absl::Status parseDeleteRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseExistsRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseGetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseSetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseCheckRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseMultiRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseReconfigRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseSetWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseSetWatches2Request(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseAddWatchRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len);
  absl::Status parseXWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                    OpCodes opcode);
  absl::Status skipString(Buffer::Instance& data, uint64_t& offset);
  absl::Status skipStrings(Buffer::Instance& data, uint64_t& offset);
  absl::Status ensureMinLength(int32_t len, int32_t minlen) const;
  absl::Status ensureMaxLength(int32_t len) const;
  absl::StatusOr<std::string> pathOnlyRequest(Buffer::Instance& data, uint64_t& offset,
                                              uint32_t len);
  absl::Status parseConnectResponse(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                    const std::chrono::milliseconds latency);
  absl::Status parseWatchEvent(Buffer::Instance& data, uint64_t& offset, uint32_t len, int64_t zxid,
                               int32_t error);
  absl::StatusOr<bool> maybeReadBool(Buffer::Instance& data, uint64_t& offset);
  absl::StatusOr<std::chrono::milliseconds> fetchControlRequestData(const int32_t xid,
                                                                    OpCodes& opcode);
  absl::StatusOr<std::chrono::milliseconds> fetchDataRequestData(const int32_t xid,
                                                                 OpCodes& opcode);

  DecoderCallbacks& callbacks_;
  const uint32_t max_packet_bytes_;
  BufferHelper helper_;
  TimeSource& time_source_;
  absl::flat_hash_map<int32_t, RequestBegin> requests_by_xid_;
  // Different from transaction ids of data requests, the transaction ids (XidCodes) of same kind of
  // control requests are always the same. Therefore, we use a queue for each kind of control
  // request, so we can differentiate different control requests with the same xid and calculate
  // their response latency.
  absl::flat_hash_map<int32_t, std::queue<RequestBegin>> control_requests_by_xid_;
  Buffer::OwnedImpl zk_filter_read_buffer_;
  Buffer::OwnedImpl zk_filter_write_buffer_;
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
