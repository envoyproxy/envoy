#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3/rate_limit.pb.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/stats/symbol_table.h"
#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include "source/extensions/filters/common/ratelimit/stat_names.h"
#include "source/extensions/filters/network/thrift_proxy/filters/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

using namespace Envoy::Extensions::NetworkFilters;

/**
 * Global configuration for Thrift rate limit filter.
 */
class Config {
public:
  Config(const envoy::extensions::filters::network::thrift_proxy::filters::ratelimit::v3::RateLimit&
             config,
         const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, Runtime::Loader& runtime,
         Upstream::ClusterManager& cm)
      : domain_(config.domain()), stage_(config.stage()), local_info_(local_info), scope_(scope),
        runtime_(runtime), cm_(cm), failure_mode_deny_(config.failure_mode_deny()),
        stat_names_(scope_.symbolTable()) {}

  const std::string& domain() const { return domain_; }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  uint32_t stage() const { return stage_; }
  Runtime::Loader& runtime() { return runtime_; }
  Upstream::ClusterManager& cm() { return cm_; }
  bool failureModeAllow() const { return !failure_mode_deny_; };
  Filters::Common::RateLimit::StatNames& statNames() { return stat_names_; }

private:
  const std::string domain_;
  const uint32_t stage_;
  const LocalInfo::LocalInfo& local_info_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
  const bool failure_mode_deny_;
  Filters::Common::RateLimit::StatNames stat_names_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * Thrift rate limit filter instance. Calls the rate limit service with the given configuration
 * parameters. If the rate limit service returns an over limit response, an application exception
 * is returned, but the downstream connection is otherwise preserved. If the rate limit service
 * allows the request, no modifications are made and further filters progress as normal. If an
 * error is returned and the failure_mode_deny option is enabled, an application exception is
 * returned. By default, errors allow the request to continue.
 */
class Filter : public ThriftProxy::ThriftFilters::PassThroughDecoderFilter,
               public Filters::Common::RateLimit::RequestCallbacks {
public:
  Filter(ConfigSharedPtr config, Filters::Common::RateLimit::ClientPtr&& client)
      : config_(std::move(config)), client_(std::move(client)) {}
  ~Filter() override = default;

  // ThriftFilters::PassThroughDecoderFilter
  void onDestroy() override;
  ThriftProxy::FilterStatus messageBegin(ThriftProxy::MessageMetadataSharedPtr) override;

  // RateLimit::RequestCallbacks
  void complete(Filters::Common::RateLimit::LimitStatus status,
                Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses,
                Http::ResponseHeaderMapPtr&& response_headers_to_add,
                Http::RequestHeaderMapPtr&& request_headers_to_add,
                const std::string& response_body,
                Filters::Common::RateLimit::DynamicMetadataPtr&& dynamic_metadata) override;

private:
  void initiateCall(const ThriftProxy::MessageMetadata& metadata);
  void populateRateLimitDescriptors(const ThriftProxy::Router::RateLimitPolicy& rate_limit_policy,
                                    std::vector<RateLimit::Descriptor>& descriptors,
                                    const ThriftProxy::Router::RouteEntry* route_entry,
                                    const ThriftProxy::MessageMetadata& headers) const;

  enum class State { NotStarted, Calling, Complete, Responded };

  ConfigSharedPtr config_;
  Filters::Common::RateLimit::ClientPtr client_;
  State state_{State::NotStarted};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  bool initiating_call_{false};
};

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
