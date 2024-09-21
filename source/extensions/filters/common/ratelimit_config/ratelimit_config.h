#pragma once

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/ratelimit/ratelimit.h"

#include "source/common/router/router_ratelimit.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

using ProtoRateLimit = envoy::extensions::common::ratelimit::v3::RateLimitPolicy;
using RateLimitDescriptors = std::vector<Envoy::RateLimit::LocalDescriptor>;

class RateLimitPolicy {
public:
  RateLimitPolicy(const ProtoRateLimit& config,
                  Server::Configuration::CommonFactoryContext& context,
                  absl::Status& creation_status);

  void populateDescriptors(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& info,
                           const std::string& local_service_cluster,
                           RateLimitDescriptors& descriptors) const;

private:
  std::vector<Envoy::RateLimit::DescriptorProducerPtr> actions_;
};

class RateLimitConfig {
public:
  RateLimitConfig(const Protobuf::RepeatedPtrField<ProtoRateLimit>& configs,
                  Server::Configuration::CommonFactoryContext& context,
                  absl::Status& creation_status);

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
