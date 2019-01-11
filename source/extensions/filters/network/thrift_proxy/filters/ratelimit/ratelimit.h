#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/thrift/rate_limit/v2alpha1/rate_limit.pb.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"

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
  Config(const envoy::config::filter::thrift::rate_limit::v2alpha1::RateLimit& config,
         const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, Runtime::Loader& runtime,
         Upstream::ClusterManager& cm)
      : domain_(config.domain()), stage_(config.stage()), local_info_(local_info), scope_(scope),
        runtime_(runtime), cm_(cm), failure_mode_deny_(config.failure_mode_deny()) {}

  const std::string& domain() const { return domain_; }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  uint32_t stage() const { return stage_; }
  Stats::Scope& scope() { return scope_; }
  Runtime::Loader& runtime() { return runtime_; }
  Upstream::ClusterManager& cm() { return cm_; }

  bool failureModeAllow() const { return !failure_mode_deny_; };

private:
  const std::string domain_;
  const uint32_t stage_;
  const LocalInfo::LocalInfo& local_info_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
  const bool failure_mode_deny_;
};

typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * Thrift rate limit filter instance. Calls the rate limit service with the given configuration
 * parameters. If the rate limit service returns an over limit response, an application exception
 * is returned, but the downstream connection is otherwise preserved. If the rate limit service
 * allows the request, no modifications are made and further filters progress as normal. If an
 * error is returned and the failure_mode_deny option is enabled, an application exception is
 * returned. By default, errors allow the request to continue.
 */
class Filter : public ThriftProxy::ThriftFilters::DecoderFilter,
               public Filters::Common::RateLimit::RequestCallbacks {
public:
  Filter(ConfigSharedPtr config, Filters::Common::RateLimit::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}
  virtual ~Filter() {}

  // ThriftFilters::ThriftDecoderFilter
  virtual void onDestroy() override;
  virtual void setDecoderFilterCallbacks(
      ThriftProxy::ThriftFilters::DecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  };
  virtual ThriftProxy::FilterStatus
  transportBegin(NetworkFilters::ThriftProxy::MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus transportEnd() override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus messageBegin(ThriftProxy::MessageMetadataSharedPtr) override;
  virtual ThriftProxy::FilterStatus messageEnd() override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus structBegin(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus structEnd() override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus fieldBegin(absl::string_view, ThriftProxy::FieldType&,
                                               int16_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus fieldEnd() override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus boolValue(bool&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus byteValue(uint8_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus int16Value(int16_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus int32Value(int32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus int64Value(int64_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus doubleValue(double&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus stringValue(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus mapBegin(ThriftProxy::FieldType&, ThriftProxy::FieldType&,
                                             uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus mapEnd() override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus listBegin(ThriftProxy::FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus listEnd() override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus setBegin(ThriftProxy::FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }
  virtual ThriftProxy::FilterStatus setEnd() override {
    return ThriftProxy::FilterStatus::Continue;
  }

  // RateLimit::RequestCallbacks
  void complete(Filters::Common::RateLimit::LimitStatus status,
                Http::HeaderMapPtr&& headers) override;

private:
  void initiateCall(const ThriftProxy::MessageMetadata& metadata);
  void populateRateLimitDescriptors(const ThriftProxy::Router::RateLimitPolicy& rate_limit_policy,
                                    std::vector<RateLimit::Descriptor>& descriptors,
                                    const ThriftProxy::Router::RouteEntry* route_entry,
                                    const ThriftProxy::MessageMetadata& headers) const;

  enum class State { NotStarted, Calling, Complete, Responded };

  ConfigSharedPtr config_;
  Filters::Common::RateLimit::ClientPtr client_;
  ThriftProxy::ThriftFilters::DecoderFilterCallbacks* callbacks_{};
  State state_{State::NotStarted};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  bool initiating_call_{false};
};

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
