#pragma once

#include "envoy/common/backoff_strategy.h"
#include "envoy/config/custom_config_validators.h"
#include "envoy/config/eds_resources_cache.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/config/xds_resources_delegate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client.h"
#include "envoy/local_info/local_info.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Config {

// Context (data) needed for creating a GrpcMux object.
// These are parameters needed for the creation of all GrpcMux objects.
struct GrpcMuxContext {
  Grpc::RawAsyncClientPtr async_client_;
  Grpc::RawAsyncClientPtr failover_async_client_;
  Event::Dispatcher& dispatcher_;
  const Protobuf::MethodDescriptor& service_method_;
  const LocalInfo::LocalInfo& local_info_;
  const RateLimitSettings& rate_limit_settings_;
  Stats::Scope& scope_;
  CustomConfigValidatorsPtr config_validators_;
  XdsResourcesDelegateOptRef xds_resources_delegate_;
  XdsConfigTrackerOptRef xds_config_tracker_;
  BackOffStrategyPtr backoff_strategy_;
  const std::string& target_xds_authority_;
  EdsResourcesCachePtr eds_resources_cache_;
};

} // namespace Config
} // namespace Envoy
