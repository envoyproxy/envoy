#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace RateLimit {
namespace TcpFilter {

/**
 * All tcp rate limit stats. @see stats_macros.h
 */
// clang-format off
#define ALL_TCP_RATE_LIMIT_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(total)                                                                                   \
  COUNTER(error)                                                                                   \
  COUNTER(over_limit)                                                                              \
  COUNTER(ok)                                                                                      \
  COUNTER(cx_closed)                                                                               \
  GAUGE  (active)
// clang-format on

/**
 * Struct definition for all tcp rate limit stats. @see stats_macros.h
 */
struct InstanceStats {
  ALL_TCP_RATE_LIMIT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Global configuration for TCP rate limit filter.
 */
class Config {
public:
  Config(const envoy::config::filter::network::rate_limit::v2::RateLimit& config,
         Stats::Scope& scope, Runtime::Loader& runtime);
  const std::string& domain() { return domain_; }
  const std::vector<Descriptor>& descriptors() { return descriptors_; }
  Runtime::Loader& runtime() { return runtime_; }
  const InstanceStats& stats() { return stats_; }

private:
  static InstanceStats generateStats(const std::string& name, Stats::Scope& scope);

  std::string domain_;
  std::vector<Descriptor> descriptors_;
  const InstanceStats stats_;
  Runtime::Loader& runtime_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * TCP rate limit filter instance. This filter will call the rate limit service with the given
 * configuration parameters. If the rate limit service returns an error or an over limit the
 * connection will be closed without any further filters being called. Otherwise all buffered
 * data will be released to further filters.
 */
class Instance : public Network::ReadFilter,
                 public Network::ConnectionCallbacks,
                 public RequestCallbacks {
public:
  Instance(ConfigSharedPtr config, ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    filter_callbacks_ = &callbacks;
    filter_callbacks_->connection().addConnectionCallbacks(*this);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // RateLimit::RequestCallbacks
  void complete(LimitStatus status) override;

private:
  enum class Status { NotStarted, Calling, Complete };

  ConfigSharedPtr config_;
  ClientPtr client_;
  Network::ReadFilterCallbacks* filter_callbacks_{};
  Status status_{Status::NotStarted};
  bool calling_limit_{};
};

} // TcpFilter
} // namespace RateLimit
} // namespace Envoy
