#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

/**
 * All tcp rate limit stats. @see stats_macros.h
 */
#define ALL_TCP_RATE_LIMIT_STATS(COUNTER, GAUGE)                                                   \
  COUNTER(cx_closed)                                                                               \
  COUNTER(error)                                                                                   \
  COUNTER(failure_mode_allowed)                                                                    \
  COUNTER(ok)                                                                                      \
  COUNTER(over_limit)                                                                              \
  COUNTER(total)                                                                                   \
  GAUGE(active, Accumulate)

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
  Config(const envoy::extensions::filters::network::ratelimit::v3::RateLimit& config,
         Stats::Scope& scope, Runtime::Loader& runtime);
  const std::string& domain() { return domain_; }
  const std::vector<RateLimit::Descriptor>& descriptors() { return descriptors_; }
  Runtime::Loader& runtime() { return runtime_; }
  const InstanceStats& stats() { return stats_; }
  bool failureModeAllow() const { return !failure_mode_deny_; };

private:
  static InstanceStats generateStats(const std::string& name, Stats::Scope& scope);

  std::string domain_;
  std::vector<RateLimit::Descriptor> descriptors_;
  const InstanceStats stats_;
  Runtime::Loader& runtime_;
  const bool failure_mode_deny_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * TCP rate limit filter instance. This filter will call the rate limit service with the given
 * configuration parameters. If the rate limit service returns an error or an over limit the
 * connection will be closed without any further filters being called. Otherwise all buffered
 * data will be released to further filters.
 */
class Filter : public Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public Filters::Common::RateLimit::RequestCallbacks {
public:
  Filter(ConfigSharedPtr config, Filters::Common::RateLimit::ClientPtr&& client)
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
  void complete(Filters::Common::RateLimit::LimitStatus status,
                Http::ResponseHeaderMapPtr&& response_headers_to_add,
                Http::RequestHeaderMapPtr&& request_headers_to_add) override;

private:
  enum class Status { NotStarted, Calling, Complete };

  ConfigSharedPtr config_;
  Filters::Common::RateLimit::ClientPtr client_;
  Network::ReadFilterCallbacks* filter_callbacks_{};
  Status status_{Status::NotStarted};
  bool calling_limit_{};
};
} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
