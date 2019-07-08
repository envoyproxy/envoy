#pragma once

#include "envoy/config/filter/fault/v2/fault.pb.h"
#include "envoy/http/header_map.h"

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Fault {

class HeaderNameValues {
public:
  const Http::LowerCaseString DelayRequest{"x-envoy-fault-delay-request"};
  const Http::LowerCaseString ThroughputResponse{"x-envoy-fault-throughput-response"};
};

using HeaderNames = ConstSingleton<HeaderNameValues>;

/**
 * Generic configuration for a delay fault.
 */
class FaultDelayConfig {
public:
  FaultDelayConfig(const envoy::config::filter::fault::v2::FaultDelay& delay_config);

  const envoy::type::FractionalPercent& percentage() const { return percentage_; }
  absl::optional<std::chrono::milliseconds> duration(const Http::HeaderEntry* header) const {
    return provider_->duration(header);
  }

private:
  // Abstract delay provider.
  class DelayProvider {
  public:
    virtual ~DelayProvider() = default;

    // Return the duration to use. Optionally passed an HTTP header that may contain the delay
    // depending on the provider implementation.
    virtual absl::optional<std::chrono::milliseconds>
    duration(const Http::HeaderEntry* header) const PURE;
  };

  // Delay provider that uses a fixed delay.
  class FixedDelayProvider : public DelayProvider {
  public:
    FixedDelayProvider(std::chrono::milliseconds delay) : delay_(delay) {}

    // DelayProvider
    absl::optional<std::chrono::milliseconds> duration(const Http::HeaderEntry*) const override {
      return delay_;
    }

  private:
    const std::chrono::milliseconds delay_;
  };

  // Delay provider the reads a delay from an HTTP header.
  class HeaderDelayProvider : public DelayProvider {
  public:
    // DelayProvider
    absl::optional<std::chrono::milliseconds>
    duration(const Http::HeaderEntry* header) const override;
  };

  using DelayProviderPtr = std::unique_ptr<DelayProvider>;

  DelayProviderPtr provider_;
  const envoy::type::FractionalPercent percentage_;
};

using FaultDelayConfigPtr = std::unique_ptr<FaultDelayConfig>;
using FaultDelayConfigSharedPtr = std::shared_ptr<FaultDelayConfig>;

/**
 * Generic configuration for a rate limit fault.
 */
class FaultRateLimitConfig {
public:
  FaultRateLimitConfig(const envoy::config::filter::fault::v2::FaultRateLimit& rate_limit_config);

  const envoy::type::FractionalPercent& percentage() const { return percentage_; }
  absl::optional<uint64_t> rateKbps(const Http::HeaderEntry* header) const {
    return provider_->rateKbps(header);
  }

private:
  // Abstract rate limit provider.
  class RateLimitProvider {
  public:
    virtual ~RateLimitProvider() = default;

    // Return the rate limit to use in KiB/s. Optionally passed an HTTP header that may contain the
    // rate limit depending on the provider implementation.
    virtual absl::optional<uint64_t> rateKbps(const Http::HeaderEntry* header) const PURE;
  };

  // Rate limit provider that uses a fixed rate limit.
  class FixedRateLimitProvider : public RateLimitProvider {
  public:
    FixedRateLimitProvider(uint64_t fixed_rate_kbps) : fixed_rate_kbps_(fixed_rate_kbps) {}
    absl::optional<uint64_t> rateKbps(const Http::HeaderEntry*) const override {
      return fixed_rate_kbps_;
    }

  private:
    const uint64_t fixed_rate_kbps_;
  };

  // Rate limit provider that reads the rate limit from an HTTP header.
  class HeaderRateLimitProvider : public RateLimitProvider {
  public:
    absl::optional<uint64_t> rateKbps(const Http::HeaderEntry* header) const override;
  };

  using RateLimitProviderPtr = std::unique_ptr<RateLimitProvider>;

  RateLimitProviderPtr provider_;
  const envoy::type::FractionalPercent percentage_;
};

using FaultRateLimitConfigPtr = std::unique_ptr<FaultRateLimitConfig>;

} // namespace Fault
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
