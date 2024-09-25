#pragma once

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/ratelimit/ratelimit.h"

#include "source/common/router/router_ratelimit.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

using ProtoRateLimit = envoy::config::route::v3::RateLimit;
using RateLimitDescriptors = std::vector<Envoy::RateLimit::LocalDescriptor>;

class RateLimitPolicy : Logger::Loggable<Envoy::Logger::Id::config> {
public:
  RateLimitPolicy(const ProtoRateLimit& config,
                  Server::Configuration::CommonFactoryContext& context,
                  absl::Status& creation_status, bool no_limit = true);

  void populateDescriptors(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& info,
                           const std::string& local_service_cluster,
                           RateLimitDescriptors& descriptors) const;

private:
  std::vector<Envoy::RateLimit::DescriptorProducerPtr> actions_;
};

class RateLimitConfig : Logger::Loggable<Envoy::Logger::Id::config> {
public:
  RateLimitConfig(const Protobuf::RepeatedPtrField<ProtoRateLimit>& configs,
                  Server::Configuration::CommonFactoryContext& context,
                  absl::Status& creation_status, bool no_limit = true);

  bool empty() const { return rate_limit_policies_.empty(); }

  size_t size() const { return rate_limit_policies_.size(); }

  void populateDescriptors(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& info,
                           const std::string& local_service_cluster,
                           RateLimitDescriptors& descriptors) const;

private:
  std::vector<std::unique_ptr<RateLimitPolicy>> rate_limit_policies_;
};

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
