#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/manager.h"
#include "envoy/tracing/http_tracer.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace RateLimit {

/**
 * A single rate limit request descriptor entry. See ratelimit.proto.
 */
struct DescriptorEntry {
  std::string key_;
  std::string value_;
};

/**
 * A single rate limit request descriptor. See ratelimit.proto.
 */
struct Descriptor {
  std::vector<DescriptorEntry> entries_;
};

/**
 * RateLimitServiceConfig that wraps the proto structure so that it can be registered as singleton.
 */
class RateLimitServiceConfig : public Singleton::Instance {

public:
  RateLimitServiceConfig(
      const envoy::config::ratelimit::v2::RateLimitServiceConfig& ratelimit_config)
      : config_(ratelimit_config) {}

  const envoy::config::ratelimit::v2::RateLimitServiceConfig& config_;
};

typedef std::shared_ptr<RateLimitServiceConfig> RateLimitServiceConfigSharedPtr;

} // namespace RateLimit
} // namespace Envoy
