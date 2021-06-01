#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/filters/network/zookeeper_proxy/decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * All ZooKeeper proxy stats. @see stats_macros.h
 */
#define ALL_ZOOKEEPER_PROXY_STATS(COUNTER)                                                         \
  COUNTER(decoder_error)                                                                           \
  COUNTER(request_bytes)                                                                           \
  COUNTER(connect_rq)                                                                              \
  COUNTER(connect_readonly_rq)                                                                     \
  COUNTER(getdata_rq)                                                                              \
  COUNTER(create_rq)                                                                               \
  COUNTER(create2_rq)                                                                              \
  COUNTER(createcontainer_rq)                                                                      \
  COUNTER(createttl_rq)                                                                            \
  COUNTER(setdata_rq)                                                                              \
  COUNTER(getchildren_rq)                                                                          \
  COUNTER(getchildren2_rq)                                                                         \
  COUNTER(getephemerals_rq)                                                                        \
  COUNTER(getallchildrennumber_rq)                                                                 \
  COUNTER(delete_rq)                                                                               \
  COUNTER(exists_rq)                                                                               \
  COUNTER(getacl_rq)                                                                               \
  COUNTER(setacl_rq)                                                                               \
  COUNTER(sync_rq)                                                                                 \
  COUNTER(ping_rq)                                                                                 \
  COUNTER(multi_rq)                                                                                \
  COUNTER(reconfig_rq)                                                                             \
  COUNTER(close_rq)                                                                                \
  COUNTER(setauth_rq)                                                                              \
  COUNTER(setwatches_rq)                                                                           \
  COUNTER(checkwatches_rq)                                                                         \
  COUNTER(removewatches_rq)                                                                        \
  COUNTER(check_rq)                                                                                \
  COUNTER(response_bytes)                                                                          \
  COUNTER(connect_resp)                                                                            \
  COUNTER(ping_resp)                                                                               \
  COUNTER(auth_resp)                                                                               \
  COUNTER(getdata_resp)                                                                            \
  COUNTER(create_resp)                                                                             \
  COUNTER(create2_resp)                                                                            \
  COUNTER(createcontainer_resp)                                                                    \
  COUNTER(createttl_resp)                                                                          \
  COUNTER(setdata_resp)                                                                            \
  COUNTER(getchildren_resp)                                                                        \
  COUNTER(getchildren2_resp)                                                                       \
  COUNTER(getephemerals_resp)                                                                      \
  COUNTER(getallchildrennumber_resp)                                                               \
  COUNTER(delete_resp)                                                                             \
  COUNTER(exists_resp)                                                                             \
  COUNTER(getacl_resp)                                                                             \
  COUNTER(setacl_resp)                                                                             \
  COUNTER(sync_resp)                                                                               \
  COUNTER(multi_resp)                                                                              \
  COUNTER(reconfig_resp)                                                                           \
  COUNTER(close_resp)                                                                              \
  COUNTER(setauth_resp)                                                                            \
  COUNTER(setwatches_resp)                                                                         \
  COUNTER(checkwatches_resp)                                                                       \
  COUNTER(removewatches_resp)                                                                      \
  COUNTER(check_resp)                                                                              \
  COUNTER(watch_event)

/**
 * Struct definition for all ZooKeeper proxy stats. @see stats_macros.h
 */
struct ZooKeeperProxyStats {
  ALL_ZOOKEEPER_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the ZooKeeper proxy filter.
 */
class ZooKeeperFilterConfig {
public:
  ZooKeeperFilterConfig(const std::string& stat_prefix, uint32_t max_packet_bytes,
                        Stats::Scope& scope);

  const ZooKeeperProxyStats& stats() { return stats_; }
  uint32_t maxPacketBytes() const { return max_packet_bytes_; }

  // Captures the counter used to track total op-code usage, as well as the
  // StatName under which to collect the latency for that op-code. The
  // latency-name will be joined with the stat_prefix_, which varies per filter
  // instance.
  struct OpCodeInfo {
    Stats::Counter* counter_;
    std::string opname_;
    Stats::StatName latency_name_;
  };

  absl::flat_hash_map<OpCodes, OpCodeInfo> op_code_map_;
  Stats::Scope& scope_;
  const uint32_t max_packet_bytes_;
  ZooKeeperProxyStats stats_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stat_prefix_;
  const Stats::StatName auth_;
  const Stats::StatName connect_latency_;
  const Stats::StatName unknown_scheme_rq_;
  const Stats::StatName unknown_opcode_latency_;

private:
  void initOpCode(OpCodes opcode, Stats::Counter& counter, absl::string_view name);

  ZooKeeperProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return ZooKeeperProxyStats{ALL_ZOOKEEPER_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using ZooKeeperFilterConfigSharedPtr = std::shared_ptr<ZooKeeperFilterConfig>;

/**
 * Implementation of ZooKeeper proxy filter.
 */
class ZooKeeperFilter : public Network::Filter,
                        DecoderCallbacks,
                        Logger::Loggable<Logger::Id::filter> {
public:
  ZooKeeperFilter(ZooKeeperFilterConfigSharedPtr config, TimeSource& time_source);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // ZooKeeperProxy::DecoderCallback
  void onDecodeError() override;
  void onRequestBytes(uint64_t bytes) override;
  void onConnect(bool readonly) override;
  void onPing() override;
  void onAuthRequest(const std::string& scheme) override;
  void onGetDataRequest(const std::string& path, bool watch) override;
  void onCreateRequest(const std::string& path, CreateFlags flags, OpCodes opcode) override;
  void onSetRequest(const std::string& path) override;
  void onGetChildrenRequest(const std::string& path, bool watch, bool v2) override;
  void onDeleteRequest(const std::string& path, int32_t version) override;
  void onExistsRequest(const std::string& path, bool watch) override;
  void onGetAclRequest(const std::string& path) override;
  void onSetAclRequest(const std::string& path, int32_t version) override;
  void onSyncRequest(const std::string& path) override;
  void onCheckRequest(const std::string& path, int32_t version) override;
  void onMultiRequest() override;
  void onReconfigRequest() override;
  void onSetWatchesRequest() override;
  void onCheckWatchesRequest(const std::string& path, int32_t type) override;
  void onRemoveWatchesRequest(const std::string& path, int32_t type) override;
  void onGetEphemeralsRequest(const std::string& path) override;
  void onGetAllChildrenNumberRequest(const std::string& path) override;
  void onCloseRequest() override;
  void onResponseBytes(uint64_t bytes) override;
  void onConnectResponse(int32_t proto_version, int32_t timeout, bool readonly,
                         const std::chrono::milliseconds& latency) override;
  void onResponse(OpCodes opcode, int32_t xid, int64_t zxid, int32_t error,
                  const std::chrono::milliseconds& latency) override;
  void onWatchEvent(int32_t event_type, int32_t client_state, const std::string& path, int64_t zxid,
                    int32_t error) override;

  DecoderPtr createDecoder(DecoderCallbacks& callbacks, TimeSource& time_source);
  void setDynamicMetadata(const std::string& key, const std::string& value);
  void setDynamicMetadata(const std::vector<std::pair<const std::string, const std::string>>& data);
  void clearDynamicMetadata();

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  ZooKeeperFilterConfigSharedPtr config_;
  std::unique_ptr<Decoder> decoder_;
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
