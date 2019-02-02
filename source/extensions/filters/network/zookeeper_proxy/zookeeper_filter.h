#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/network/zookeeper_proxy/zookeeper_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * All ZooKeeper proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_ZOOKEEPER_PROXY_STATS(COUNTER)                              \
  COUNTER(decoder_error)                                                \
  COUNTER(connect)                                                      \
  COUNTER(connect_readonly)                                             \
  COUNTER(getdata_rq)                                                   \
  COUNTER(create_rq)                                                    \
  COUNTER(create2_rq)                                                   \
  COUNTER(setdata_rq)                                                   \
  COUNTER(getchildren_rq)                                               \
  COUNTER(getchildren2_rq)                                              \
  COUNTER(remove)                                                       \
  COUNTER(exists)                                                       \
  COUNTER(getacl)                                                       \
  COUNTER(setacl)                                                       \
  COUNTER(sync)                                                         \
  COUNTER(ping)                                                         \
  COUNTER(multi)                                                        \
  COUNTER(create2)                                                      \
  COUNTER(reconfig)                                                     \
  COUNTER(createsession)                                                \
  COUNTER(close)                                                        \
  COUNTER(setauth)                                                      \
  COUNTER(setwatches)                                                   \
// clang-format on

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
  ZooKeeperFilterConfig(const std::string &stat_prefix, Stats::Scope& scope);

  const ZooKeeperProxyStats& stats() { return stats_; }

  Stats::Scope& scope_;
  const std::string stat_prefix_;
  ZooKeeperProxyStats stats_;

private:
  ZooKeeperProxyStats generateStats(const std::string& prefix,
                                Stats::Scope& scope) {
    return ZooKeeperProxyStats{
        ALL_ZOOKEEPER_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }
};

using ZooKeeperFilterConfigSharedPtr = std::shared_ptr<ZooKeeperFilterConfig>;

/**
 * Implementation of ZooKeeper proxy filter.
 */
class ZooKeeperFilter : public Network::Filter, DecoderCallbacks, Logger::Loggable<Logger::Id::filter> {
public:
  ZooKeeperFilter(ZooKeeperFilterConfigSharedPtr config);
  ~ZooKeeperFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // ZooKeeperProxy::DecoderCallback
  void onDecodeError() override;
  void onConnect(const bool readonly) override;
  void onPing() override;
  void onAuthRequest(const std::string& scheme) override;
  void onGetDataRequest(const std::string& path, const bool watch) override;
  void onCreateRequest(const std::string& path, const bool ephemeral, const bool sequence, const bool two) override;
  void onSetRequest(const std::string& path) override;
  void onGetChildrenRequest(const std::string& path, const bool watch, const bool two) override;

  void doDecode(Buffer::Instance& buffer);
  DecoderPtr createDecoder(DecoderCallbacks& callbacks);

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  ZooKeeperFilterConfigSharedPtr config_;
  std::unique_ptr<Decoder> decoder_;
};

}  // namespace ZooKeeperProxy
}  // namespace NetworkFilters
}  // namespace Extensions
}  // namespace Envoy
