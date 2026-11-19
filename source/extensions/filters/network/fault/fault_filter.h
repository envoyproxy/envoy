#pragma once

#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/fault/v3/fault.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Fault {

/**
 * All stats for the network fault filter. @see stats_macros.h
 */
#define ALL_NETWORK_FAULT_FILTER_STATS(COUNTER)                                                    \
  COUNTER(aborts_injected)                                                                         \
  COUNTER(aborts_dry_run)

/**
 * Wrapper struct for network fault filter stats. @see stats_macros.h
 */
struct FaultFilterStats {
  ALL_NETWORK_FAULT_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the network fault filter.
 */
class FaultFilterConfig {
public:
  FaultFilterConfig(
      const envoy::extensions::filters::network::fault::v3::NetworkFault& proto_config,
      Stats::Scope& scope, Runtime::Loader& runtime);

  bool hasAbort() const { return has_abort_; }
  const envoy::type::v3::FractionalPercent& abortPercentage() const { return abort_percentage_; }
  bool isAbortDryRun() const { return dry_run_; }
  const std::string& abortPercentRuntime() const { return abort_percent_runtime_; }
  bool hasFilterMetadata() const { return has_filter_metadata_; }
  const Envoy::Protobuf::Struct& filterMetadata() const { return filter_metadata_; }

  Runtime::Loader& runtime() { return runtime_; }
  FaultFilterStats& stats() { return stats_; }
  const FaultFilterStats& stats() const { return stats_; }

private:
  class RuntimeKeyValues {
  public:
    const std::string AbortPercentKey = "fault.network.abort.abort_percent";
  };
  using RuntimeKeys = ConstSingleton<RuntimeKeyValues>;

  static FaultFilterStats generateStats(absl::string_view stat_prefix, Stats::Scope& scope);

  const bool has_abort_;
  const envoy::type::v3::FractionalPercent abort_percentage_;
  const bool dry_run_;
  const std::string abort_percent_runtime_;
  const bool has_filter_metadata_;
  const Envoy::Protobuf::Struct filter_metadata_;
  Runtime::Loader& runtime_;
  FaultFilterStats stats_;
};

using FaultFilterConfigSharedPtr = std::shared_ptr<FaultFilterConfig>;

/**
 * A network read filter capable of aborting incoming connections (or evaluating aborts in dry-run
 * mode) before dispatching them to downstream network filters such as tcp_proxy.
 */
class FaultFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit FaultFilter(FaultFilterConfigSharedPtr config) : config_(std::move(config)) {}
  ~FaultFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  bool isAbortEnabled() const;
  void recordDynamicMetadata();
  void recordAbortDryRun();
  void abortConnection();

  FaultFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  bool evaluated_{false};
  bool aborted_{false};
};

} // namespace Fault
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
