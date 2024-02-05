#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.h"
#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/filters/network/zookeeper_proxy/decoder.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * All ZooKeeper proxy stats. @see stats_macros.h
 */
#define ALL_ZOOKEEPER_PROXY_STATS(COUNTER)                                                         \
  COUNTER(decoder_error)                                                                           \
  COUNTER(connect_decoder_error)                                                                   \
  COUNTER(ping_decoder_error)                                                                      \
  COUNTER(auth_decoder_error)                                                                      \
  COUNTER(getdata_decoder_error)                                                                   \
  COUNTER(create_decoder_error)                                                                    \
  COUNTER(create2_decoder_error)                                                                   \
  COUNTER(createcontainer_decoder_error)                                                           \
  COUNTER(createttl_decoder_error)                                                                 \
  COUNTER(setdata_decoder_error)                                                                   \
  COUNTER(getchildren_decoder_error)                                                               \
  COUNTER(getchildren2_decoder_error)                                                              \
  COUNTER(getephemerals_decoder_error)                                                             \
  COUNTER(getallchildrennumber_decoder_error)                                                      \
  COUNTER(delete_decoder_error)                                                                    \
  COUNTER(exists_decoder_error)                                                                    \
  COUNTER(getacl_decoder_error)                                                                    \
  COUNTER(setacl_decoder_error)                                                                    \
  COUNTER(sync_decoder_error)                                                                      \
  COUNTER(multi_decoder_error)                                                                     \
  COUNTER(reconfig_decoder_error)                                                                  \
  COUNTER(close_decoder_error)                                                                     \
  COUNTER(setauth_decoder_error)                                                                   \
  COUNTER(setwatches_decoder_error)                                                                \
  COUNTER(setwatches2_decoder_error)                                                               \
  COUNTER(addwatch_decoder_error)                                                                  \
  COUNTER(checkwatches_decoder_error)                                                              \
  COUNTER(removewatches_decoder_error)                                                             \
  COUNTER(check_decoder_error)                                                                     \
  COUNTER(request_bytes)                                                                           \
  COUNTER(connect_rq_bytes)                                                                        \
  COUNTER(connect_readonly_rq_bytes)                                                               \
  COUNTER(ping_rq_bytes)                                                                           \
  COUNTER(auth_rq_bytes)                                                                           \
  COUNTER(getdata_rq_bytes)                                                                        \
  COUNTER(create_rq_bytes)                                                                         \
  COUNTER(create2_rq_bytes)                                                                        \
  COUNTER(createcontainer_rq_bytes)                                                                \
  COUNTER(createttl_rq_bytes)                                                                      \
  COUNTER(setdata_rq_bytes)                                                                        \
  COUNTER(getchildren_rq_bytes)                                                                    \
  COUNTER(getchildren2_rq_bytes)                                                                   \
  COUNTER(getephemerals_rq_bytes)                                                                  \
  COUNTER(getallchildrennumber_rq_bytes)                                                           \
  COUNTER(delete_rq_bytes)                                                                         \
  COUNTER(exists_rq_bytes)                                                                         \
  COUNTER(getacl_rq_bytes)                                                                         \
  COUNTER(setacl_rq_bytes)                                                                         \
  COUNTER(sync_rq_bytes)                                                                           \
  COUNTER(multi_rq_bytes)                                                                          \
  COUNTER(reconfig_rq_bytes)                                                                       \
  COUNTER(close_rq_bytes)                                                                          \
  COUNTER(setauth_rq_bytes)                                                                        \
  COUNTER(setwatches_rq_bytes)                                                                     \
  COUNTER(setwatches2_rq_bytes)                                                                    \
  COUNTER(addwatch_rq_bytes)                                                                       \
  COUNTER(checkwatches_rq_bytes)                                                                   \
  COUNTER(removewatches_rq_bytes)                                                                  \
  COUNTER(check_rq_bytes)                                                                          \
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
  COUNTER(setwatches2_rq)                                                                          \
  COUNTER(addwatch_rq)                                                                             \
  COUNTER(checkwatches_rq)                                                                         \
  COUNTER(removewatches_rq)                                                                        \
  COUNTER(check_rq)                                                                                \
  COUNTER(response_bytes)                                                                          \
  COUNTER(connect_resp_bytes)                                                                      \
  COUNTER(ping_resp_bytes)                                                                         \
  COUNTER(auth_resp_bytes)                                                                         \
  COUNTER(getdata_resp_bytes)                                                                      \
  COUNTER(create_resp_bytes)                                                                       \
  COUNTER(create2_resp_bytes)                                                                      \
  COUNTER(createcontainer_resp_bytes)                                                              \
  COUNTER(createttl_resp_bytes)                                                                    \
  COUNTER(setdata_resp_bytes)                                                                      \
  COUNTER(getchildren_resp_bytes)                                                                  \
  COUNTER(getchildren2_resp_bytes)                                                                 \
  COUNTER(getephemerals_resp_bytes)                                                                \
  COUNTER(getallchildrennumber_resp_bytes)                                                         \
  COUNTER(delete_resp_bytes)                                                                       \
  COUNTER(exists_resp_bytes)                                                                       \
  COUNTER(getacl_resp_bytes)                                                                       \
  COUNTER(setacl_resp_bytes)                                                                       \
  COUNTER(sync_resp_bytes)                                                                         \
  COUNTER(multi_resp_bytes)                                                                        \
  COUNTER(reconfig_resp_bytes)                                                                     \
  COUNTER(close_resp_bytes)                                                                        \
  COUNTER(setauth_resp_bytes)                                                                      \
  COUNTER(setwatches_resp_bytes)                                                                   \
  COUNTER(setwatches2_resp_bytes)                                                                  \
  COUNTER(addwatch_resp_bytes)                                                                     \
  COUNTER(checkwatches_resp_bytes)                                                                 \
  COUNTER(removewatches_resp_bytes)                                                                \
  COUNTER(check_resp_bytes)                                                                        \
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
  COUNTER(setwatches2_resp)                                                                        \
  COUNTER(addwatch_resp)                                                                           \
  COUNTER(checkwatches_resp)                                                                       \
  COUNTER(removewatches_resp)                                                                      \
  COUNTER(check_resp)                                                                              \
  COUNTER(watch_event)                                                                             \
  COUNTER(connect_resp_fast)                                                                       \
  COUNTER(ping_resp_fast)                                                                          \
  COUNTER(auth_resp_fast)                                                                          \
  COUNTER(getdata_resp_fast)                                                                       \
  COUNTER(create_resp_fast)                                                                        \
  COUNTER(create2_resp_fast)                                                                       \
  COUNTER(createcontainer_resp_fast)                                                               \
  COUNTER(createttl_resp_fast)                                                                     \
  COUNTER(setdata_resp_fast)                                                                       \
  COUNTER(getchildren_resp_fast)                                                                   \
  COUNTER(getchildren2_resp_fast)                                                                  \
  COUNTER(getephemerals_resp_fast)                                                                 \
  COUNTER(getallchildrennumber_resp_fast)                                                          \
  COUNTER(delete_resp_fast)                                                                        \
  COUNTER(exists_resp_fast)                                                                        \
  COUNTER(getacl_resp_fast)                                                                        \
  COUNTER(setacl_resp_fast)                                                                        \
  COUNTER(sync_resp_fast)                                                                          \
  COUNTER(multi_resp_fast)                                                                         \
  COUNTER(reconfig_resp_fast)                                                                      \
  COUNTER(close_resp_fast)                                                                         \
  COUNTER(setauth_resp_fast)                                                                       \
  COUNTER(setwatches_resp_fast)                                                                    \
  COUNTER(setwatches2_resp_fast)                                                                   \
  COUNTER(addwatch_resp_fast)                                                                      \
  COUNTER(checkwatches_resp_fast)                                                                  \
  COUNTER(removewatches_resp_fast)                                                                 \
  COUNTER(check_resp_fast)                                                                         \
  COUNTER(connect_resp_slow)                                                                       \
  COUNTER(ping_resp_slow)                                                                          \
  COUNTER(auth_resp_slow)                                                                          \
  COUNTER(getdata_resp_slow)                                                                       \
  COUNTER(create_resp_slow)                                                                        \
  COUNTER(create2_resp_slow)                                                                       \
  COUNTER(createcontainer_resp_slow)                                                               \
  COUNTER(createttl_resp_slow)                                                                     \
  COUNTER(setdata_resp_slow)                                                                       \
  COUNTER(getchildren_resp_slow)                                                                   \
  COUNTER(getchildren2_resp_slow)                                                                  \
  COUNTER(getephemerals_resp_slow)                                                                 \
  COUNTER(getallchildrennumber_resp_slow)                                                          \
  COUNTER(delete_resp_slow)                                                                        \
  COUNTER(exists_resp_slow)                                                                        \
  COUNTER(getacl_resp_slow)                                                                        \
  COUNTER(setacl_resp_slow)                                                                        \
  COUNTER(sync_resp_slow)                                                                          \
  COUNTER(multi_resp_slow)                                                                         \
  COUNTER(reconfig_resp_slow)                                                                      \
  COUNTER(close_resp_slow)                                                                         \
  COUNTER(setauth_resp_slow)                                                                       \
  COUNTER(setwatches_resp_slow)                                                                    \
  COUNTER(setwatches2_resp_slow)                                                                   \
  COUNTER(addwatch_resp_slow)                                                                      \
  COUNTER(checkwatches_resp_slow)                                                                  \
  COUNTER(removewatches_resp_slow)                                                                 \
  COUNTER(check_resp_slow)

/**
 * Struct definition for all ZooKeeper proxy stats. @see stats_macros.h
 */
struct ZooKeeperProxyStats {
  ALL_ZOOKEEPER_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

enum class ErrorBudgetResponseType { Fast, Slow, None };

using envoy::extensions::filters::network::zookeeper_proxy::v3::LatencyThresholdOverride;
using envoy::extensions::filters::network::zookeeper_proxy::v3::LatencyThresholdOverride_Opcode;
using LatencyThresholdOverrideList = Protobuf::RepeatedPtrField<LatencyThresholdOverride>;
using LatencyThresholdOverrideMap = absl::flat_hash_map<int32_t, std::chrono::milliseconds>;
using OpcodeMap = absl::flat_hash_map<LatencyThresholdOverride_Opcode, int32_t>;

/**
 * Configuration for the ZooKeeper proxy filter.
 */
class ZooKeeperFilterConfig {
public:
  ZooKeeperFilterConfig(const std::string& stat_prefix, const uint32_t max_packet_bytes,
                        const bool enable_per_opcode_request_bytes_metrics,
                        const bool enable_per_opcode_response_bytes_metrics,
                        const bool enable_per_opcode_decoder_error_metrics,
                        const bool enable_latency_threshold_metrics,
                        const std::chrono::milliseconds default_latency_threshold,
                        const LatencyThresholdOverrideList& latency_threshold_overrides,
                        Stats::Scope& scope);

  const ZooKeeperProxyStats& stats() { return stats_; }
  uint32_t maxPacketBytes() const { return max_packet_bytes_; }

  // The OpCodeInfo is created as a public member of ZooKeeperFilterConfig.
  // Therefore, its lifetime is tied to that of ZooKeeperFilterConfig.
  // When the ZooKeeperFilterConfig object is destroyed, the OpCodeInfo will be destroyed as well.
  // The the lifetime of scope is tied to the context passed to network filters to access server
  // resources. The values of counter elements in OpCodeInfo are used to track total op-code usage,
  // as well as the StatName under which to collect the latency, fast/slow responses for that
  // op-code. The latency-name will be joined with the stat_prefix_, which varies per filter
  // instance.
  struct OpCodeInfo {
    Stats::Counter* resp_counter_;
    Stats::Counter* resp_fast_counter_;
    Stats::Counter* resp_slow_counter_;
    Stats::Counter* rq_bytes_counter_;
    Stats::Counter* resp_bytes_counter_;
    Stats::Counter* decoder_error_counter_;
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
  const bool enable_per_opcode_request_bytes_metrics_;
  const bool enable_per_opcode_response_bytes_metrics_;
  const bool enable_per_opcode_decoder_error_metrics_;

  ErrorBudgetResponseType errorBudgetDecision(const OpCodes opcode,
                                              const std::chrono::milliseconds latency) const;

private:
  void initOpCode(OpCodes opcode, Stats::Counter& resp_counter, Stats::Counter& resp_fast_counter,
                  Stats::Counter& resp_slow_counter, Stats::Counter& rq_bytes_counter,
                  Stats::Counter& resp_bytes_counter, Stats::Counter& decoder_error_counter,
                  absl::string_view name);

  ZooKeeperProxyStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return ZooKeeperProxyStats{ALL_ZOOKEEPER_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  static const OpcodeMap& opcodeMap() {
    CONSTRUCT_ON_FIRST_USE(OpcodeMap, {{LatencyThresholdOverride::Connect, 0},
                                       {LatencyThresholdOverride::Create, 1},
                                       {LatencyThresholdOverride::Delete, 2},
                                       {LatencyThresholdOverride::Exists, 3},
                                       {LatencyThresholdOverride::GetData, 4},
                                       {LatencyThresholdOverride::SetData, 5},
                                       {LatencyThresholdOverride::GetAcl, 6},
                                       {LatencyThresholdOverride::SetAcl, 7},
                                       {LatencyThresholdOverride::GetChildren, 8},
                                       {LatencyThresholdOverride::Sync, 9},
                                       {LatencyThresholdOverride::Ping, 11},
                                       {LatencyThresholdOverride::GetChildren2, 12},
                                       {LatencyThresholdOverride::Check, 13},
                                       {LatencyThresholdOverride::Multi, 14},
                                       {LatencyThresholdOverride::Create2, 15},
                                       {LatencyThresholdOverride::Reconfig, 16},
                                       {LatencyThresholdOverride::CheckWatches, 17},
                                       {LatencyThresholdOverride::RemoveWatches, 18},
                                       {LatencyThresholdOverride::CreateContainer, 19},
                                       {LatencyThresholdOverride::CreateTtl, 21},
                                       {LatencyThresholdOverride::Close, -11},
                                       {LatencyThresholdOverride::SetAuth, 100},
                                       {LatencyThresholdOverride::SetWatches, 101},
                                       {LatencyThresholdOverride::GetEphemerals, 103},
                                       {LatencyThresholdOverride::GetAllChildrenNumber, 104},
                                       {LatencyThresholdOverride::SetWatches2, 105},
                                       {LatencyThresholdOverride::AddWatch, 106}});
  }

  int32_t getOpCodeIndex(LatencyThresholdOverride_Opcode opcode);
  LatencyThresholdOverrideMap
  parseLatencyThresholdOverrides(const LatencyThresholdOverrideList& latency_threshold_overrides);

  const bool enable_latency_threshold_metrics_;
  const std::chrono::milliseconds default_latency_threshold_;
  // Key: opcode enum value defined in decoder.h, value: latency threshold override in millisecond.
  const LatencyThresholdOverrideMap latency_threshold_override_map_;
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
  void onDecodeError(const absl::optional<OpCodes> opcode) override;
  void onRequestBytes(const absl::optional<OpCodes> opcode, const uint64_t bytes) override;
  void onConnect(bool readonly) override;
  void onPing() override;
  void onAuthRequest(const std::string& scheme) override;
  void onGetDataRequest(const std::string& path, bool watch) override;
  absl::Status onCreateRequest(const std::string& path, CreateFlags flags, OpCodes opcode) override;
  void onSetRequest(const std::string& path) override;
  void onGetChildrenRequest(const std::string& path, bool watch, bool v2) override;
  void onDeleteRequest(const std::string& path, int32_t version) override;
  void onExistsRequest(const std::string& path, bool watch) override;
  void onGetAclRequest(const std::string& path) override;
  void onSetAclRequest(const std::string& path, int32_t version) override;
  absl::Status onSyncRequest(const absl::StatusOr<std::string>& path,
                             const OpCodes opcode) override;
  void onCheckRequest(const std::string& path, int32_t version) override;
  void onMultiRequest() override;
  void onReconfigRequest() override;
  void onSetWatchesRequest() override;
  void onSetWatches2Request() override;
  void onAddWatchRequest(const std::string& path, const int32_t mode) override;
  void onCheckWatchesRequest(const std::string& path, int32_t type) override;
  void onRemoveWatchesRequest(const std::string& path, int32_t type) override;
  absl::Status onGetEphemeralsRequest(const absl::StatusOr<std::string>& path,
                                      const OpCodes opcode) override;
  absl::Status onGetAllChildrenNumberRequest(const absl::StatusOr<std::string>& path,
                                             const OpCodes opcode) override;
  void onCloseRequest() override;
  void onResponseBytes(const absl::optional<OpCodes> opcode, const uint64_t bytes) override;
  void onConnectResponse(int32_t proto_version, int32_t timeout, bool readonly,
                         const std::chrono::milliseconds latency) override;
  void onResponse(OpCodes opcode, int32_t xid, int64_t zxid, int32_t error,
                  const std::chrono::milliseconds latency) override;
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
