#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/filter/network/ext_authz.pb.h"
#include "envoy/ext_authz/ext_authz.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/ext_authz/ext_authz_impl.h"

namespace Envoy {
namespace ExtAuthz {
namespace TcpFilter {

/**
 * All tcp external authorization stats. @see stats_macros.h
 */
// clang-format off
#define ALL_TCP_EXT_AUTHZ_STATS(COUNTER, GAUGE)         \
  COUNTER(total)                                        \
  COUNTER(error)                                        \
  COUNTER(unauthz)                                      \
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
  // @saumoh: TBD: Take care of grpc service != envoy_grpc()
  Config(const envoy::api::v2::filter::network::ExtAuthz& config, Stats::Scope& scope,
         Runtime::Loader& runtime, Upstream::ClusterManager& cm)
      : stats_(generateStats(config.stat_prefix(), scope)), runtime_(runtime), cm_(cm),
        cluster_name_(config.grpc_service().envoy_grpc().cluster_name()),
        failure_mode_allow_(config.failure_mode_allow()) {}

  Runtime::Loader& runtime() { return runtime_; }
  std::string cluster() { return cluster_name_; }
  Upstream::ClusterManager& cm() { return cm_; }
  const InstanceStats& stats() { return stats_; }
  bool failOpen() const { return failure_mode_allow_; }

private:
  static InstanceStats generateStats(const std::string& name, Stats::Scope& scope);
  const InstanceStats stats_;
  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
  std::string cluster_name_;
  bool failure_mode_allow_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * ExtAuthz filter instance. This filter will call the Authorization service with the given
 * configuration parameters. If the authorization service returns an error or a deny the
 * connection will be closed without any further filters being called. Otherwise all buffered
 * data will be released to further filters.
 */
class Instance : public Network::ReadFilter,
                 public Network::ConnectionCallbacks,
                 public RequestCallbacks {
public:
  Instance(ConfigSharedPtr config, ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}
  ~Instance() {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
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
  void complete(CheckStatus status) override;

  void setCheckReqGenerator(CheckRequestGenerator* crg);

private:
  enum class Status { NotStarted, Calling, Complete };
  void callCheck();

  ConfigSharedPtr config_;
  ClientPtr client_;
  Network::ReadFilterCallbacks* filter_callbacks_{};
  Status status_{Status::NotStarted};
  bool calling_check_{};
  CheckRequestGeneratorPtr check_req_generator_{};
};

} // TcpFilter
} // namespace ExtAuthz
} // namespace Envoy
