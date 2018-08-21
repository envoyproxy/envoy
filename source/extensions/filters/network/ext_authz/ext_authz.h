#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/network/ext_authz/v2/ext_authz.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/common/ext_authz/ext_authz.h"
#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

/**
 * All tcp external authorization stats. @see stats_macros.h
 */
// clang-format off
#define ALL_TCP_EXT_AUTHZ_STATS(COUNTER, GAUGE)         \
  COUNTER(total)                                        \
  COUNTER(error)                                        \
  COUNTER(denied)                                       \
  COUNTER(failure_mode_allowed)                         \
  COUNTER(ok)                                           \
  COUNTER(cx_closed)                                    \
  GAUGE  (active)
// clang-format on

/**
 * Struct definition for all external authorization stats. @see stats_macros.h
 */
struct InstanceStats {
  ALL_TCP_EXT_AUTHZ_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Global configuration for ExtAuthz filter.
 */
class Config {
public:
  Config(const envoy::config::filter::network::ext_authz::v2::ExtAuthz& config, Stats::Scope& scope)
      : stats_(generateStats(config.stat_prefix(), scope)),
        failure_mode_allow_(config.failure_mode_allow()) {}

  const InstanceStats& stats() { return stats_; }
  bool failureModeAllow() const { return failure_mode_allow_; }
  void setFailModeAllow(bool value) { failure_mode_allow_ = value; }

private:
  static InstanceStats generateStats(const std::string& name, Stats::Scope& scope);
  const InstanceStats stats_;
  bool failure_mode_allow_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * ExtAuthz filter instance. This filter will call the Authorization service with the given
 * configuration parameters. If the authorization service returns an error or a deny the
 * connection will be closed without any further filters being called. Otherwise all buffered
 * data will be released to further filters.
 */
class Filter : public Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public Filters::Common::ExtAuthz::RequestCallbacks {
public:
  Filter(ConfigSharedPtr config, Filters::Common::ExtAuthz::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}
  ~Filter() {}

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

  // ExtAuthz::RequestCallbacks
  void onComplete(Filters::Common::ExtAuthz::ResponsePtr&&) override;

private:
  // State of this filter's communication with the external authorization service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class Status { NotStarted, Calling, Complete };
  // FilterReturn is used to capture what the return code should be to the filter chain.
  // if this filter is either in the middle of calling the external service or the result is denied
  // then the filter chain should stop. Otherwise the filter chain can continue to the next filter.
  enum class FilterReturn { Stop, Continue };
  void callCheck();

  ConfigSharedPtr config_;
  Filters::Common::ExtAuthz::ClientPtr client_;
  Network::ReadFilterCallbacks* filter_callbacks_{};
  Status status_{Status::NotStarted};
  FilterReturn filter_return_{FilterReturn::Stop};
  // Used to identify if the callback to onComplete() is synchronous (on the stack) or asynchronous.
  bool calling_check_{};
  envoy::service::auth::v2alpha::CheckRequest check_request_{};
};
} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
