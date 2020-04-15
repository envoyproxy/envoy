#pragma once

#include "envoy/extensions/filters/common/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Fault {

class HeaderNameValues {
public:
  const char* prefix() { return ThreadSafeSingleton<Http::PrefixValue>::get().prefix(); }

  const Http::LowerCaseString AbortRequest{absl::StrCat(prefix(), "-fault-abort-request")};
  const Http::LowerCaseString AbortRequestPercentage{
      absl::StrCat(prefix(), "-fault-abort-request-percentage")};
  const Http::LowerCaseString DelayRequest{absl::StrCat(prefix(), "-fault-delay-request")};
  const Http::LowerCaseString DelayRequestPercentage{
      absl::StrCat(prefix(), "-fault-delay-request-percentage")};
  const Http::LowerCaseString ThroughputResponse{
      absl::StrCat(prefix(), "-fault-throughput-response")};
  const Http::LowerCaseString ThroughputResponsePercentage{
      absl::StrCat(prefix(), "-fault-throughput-response-percentage")};
};

using HeaderNames = ConstSingleton<HeaderNameValues>;

class HeaderPercentageProvider {
public:
  HeaderPercentageProvider(const Http::LowerCaseString& header_name,
                           const envoy::type::v3::FractionalPercent& percentage)
      : header_name_(header_name), percentage_(percentage) {}

  // Return the percentage. Optionally passed HTTP headers that may contain the percentage number,
  // otherwise the percentage passed at the initialized time is returned.
  envoy::type::v3::FractionalPercent
  percentage(const Http::RequestHeaderMap* request_headers) const;

private:
  const Http::LowerCaseString header_name_;
  const envoy::type::v3::FractionalPercent percentage_;
};

class FaultAbortConfig {
public:
  FaultAbortConfig(const envoy::extensions::filters::http::fault::v3::FaultAbort& abort_config);

  absl::optional<Http::Code> statusCode(const Http::RequestHeaderMap* request_headers) const {
    return provider_->statusCode(request_headers);
  }

  envoy::type::v3::FractionalPercent
  percentage(const Http::RequestHeaderMap* request_headers) const {
    return provider_->percentage(request_headers);
  }

private:
  // Abstract abort provider.
  class AbortProvider {
  public:
    virtual ~AbortProvider() = default;

    // Return the HTTP status code to use. Optionally passed HTTP headers that may contain the
    // HTTP status code depending on the provider implementation.
    virtual absl::optional<Http::Code>
    statusCode(const Http::RequestHeaderMap* request_headers) const PURE;
    // Return what percentage of requests abort faults should be applied to. Optionally passed
    // HTTP headers that may contain the percentage depending on the provider implementation.
    virtual envoy::type::v3::FractionalPercent
    percentage(const Http::RequestHeaderMap* request_headers) const PURE;
  };

  // Delay provider that uses a fixed abort status code.
  class FixedAbortProvider : public AbortProvider {
  public:
    FixedAbortProvider(uint64_t status_code, const envoy::type::v3::FractionalPercent& percentage)
        : status_code_(status_code), percentage_(percentage) {}

    // AbortProvider
    absl::optional<Http::Code> statusCode(const Http::RequestHeaderMap*) const override {
      return static_cast<Http::Code>(status_code_);
    }

    envoy::type::v3::FractionalPercent percentage(const Http::RequestHeaderMap*) const override {
      return percentage_;
    }

  private:
    const uint64_t status_code_;
    const envoy::type::v3::FractionalPercent percentage_;
  };

  // Abort provider the reads a status code from an HTTP header.
  class HeaderAbortProvider : public AbortProvider, public HeaderPercentageProvider {
  public:
    HeaderAbortProvider(const envoy::type::v3::FractionalPercent& percentage)
        : HeaderPercentageProvider(HeaderNames::get().AbortRequestPercentage, percentage) {}
    // AbortProvider
    absl::optional<Http::Code>
    statusCode(const Http::RequestHeaderMap* request_headers) const override;

    envoy::type::v3::FractionalPercent
    percentage(const Http::RequestHeaderMap* request_headers) const override {
      return HeaderPercentageProvider::percentage(request_headers);
    }
  };

  using AbortProviderPtr = std::unique_ptr<AbortProvider>;

  AbortProviderPtr provider_;
};

using FaultAbortConfigPtr = std::unique_ptr<FaultAbortConfig>;

/**
 * Generic configuration for a delay fault.
 */
class FaultDelayConfig {
public:
  FaultDelayConfig(const envoy::extensions::filters::common::fault::v3::FaultDelay& delay_config);

  absl::optional<std::chrono::milliseconds>
  duration(const Http::RequestHeaderMap* request_headers) const {
    return provider_->duration(request_headers);
  }

  envoy::type::v3::FractionalPercent
  percentage(const Http::RequestHeaderMap* request_headers) const {
    return provider_->percentage(request_headers);
  }

private:
  // Abstract delay provider.
  class DelayProvider {
  public:
    virtual ~DelayProvider() = default;

    // Return the duration to use. Optionally passed HTTP headers that may contain the delay
    // depending on the provider implementation.
    virtual absl::optional<std::chrono::milliseconds>
    duration(const Http::RequestHeaderMap* request_headers) const PURE;
    // Return what percentage of requests request faults should be applied to. Optionally passed
    // HTTP headers that may contain the percentage depending on the provider implementation.
    virtual envoy::type::v3::FractionalPercent
    percentage(const Http::RequestHeaderMap* request_headers) const PURE;
  };

  // Delay provider that uses a fixed delay.
  class FixedDelayProvider : public DelayProvider {
  public:
    FixedDelayProvider(std::chrono::milliseconds delay,
                       const envoy::type::v3::FractionalPercent& percentage)
        : delay_(delay), percentage_(percentage) {}

    // DelayProvider
    absl::optional<std::chrono::milliseconds>
    duration(const Http::RequestHeaderMap*) const override {
      return delay_;
    }

    envoy::type::v3::FractionalPercent percentage(const Http::RequestHeaderMap*) const override {
      return percentage_;
    }

  private:
    const std::chrono::milliseconds delay_;
    const envoy::type::v3::FractionalPercent percentage_;
  };

  // Delay provider the reads a delay from an HTTP header.
  class HeaderDelayProvider : public DelayProvider, public HeaderPercentageProvider {
  public:
    HeaderDelayProvider(const envoy::type::v3::FractionalPercent& percentage)
        : HeaderPercentageProvider(HeaderNames::get().DelayRequestPercentage, percentage) {}
    // DelayProvider
    absl::optional<std::chrono::milliseconds>
    duration(const Http::RequestHeaderMap* request_headers) const override;

    envoy::type::v3::FractionalPercent
    percentage(const Http::RequestHeaderMap* request_headers) const override {
      return HeaderPercentageProvider::percentage(request_headers);
    }
  };

  using DelayProviderPtr = std::unique_ptr<DelayProvider>;

  DelayProviderPtr provider_;
};

using FaultDelayConfigPtr = std::unique_ptr<FaultDelayConfig>;
using FaultDelayConfigSharedPtr = std::shared_ptr<FaultDelayConfig>;

/**
 * Generic configuration for a rate limit fault.
 */
class FaultRateLimitConfig {
public:
  FaultRateLimitConfig(
      const envoy::extensions::filters::common::fault::v3::FaultRateLimit& rate_limit_config);

  absl::optional<uint64_t> rateKbps(const Http::RequestHeaderMap* request_headers) const {
    return provider_->rateKbps(request_headers);
  }

  envoy::type::v3::FractionalPercent
  percentage(const Http::RequestHeaderMap* request_headers) const {
    return provider_->percentage(request_headers);
  }

private:
  // Abstract rate limit provider.
  class RateLimitProvider {
  public:
    virtual ~RateLimitProvider() = default;

    // Return the rate limit to use in KiB/s. Optionally passed HTTP headers that may contain the
    // rate limit depending on the provider implementation.
    virtual absl::optional<uint64_t>
    rateKbps(const Http::RequestHeaderMap* request_headers) const PURE;
    // Return what percentage of requests response rate limit faults should be applied to.
    // Optionally passed HTTP headers that may contain the percentage depending on the provider
    // implementation.
    virtual envoy::type::v3::FractionalPercent
    percentage(const Http::RequestHeaderMap* request_headers) const PURE;
  };

  // Rate limit provider that uses a fixed rate limit.
  class FixedRateLimitProvider : public RateLimitProvider {
  public:
    FixedRateLimitProvider(uint64_t fixed_rate_kbps,
                           const envoy::type::v3::FractionalPercent& percentage)
        : fixed_rate_kbps_(fixed_rate_kbps), percentage_(percentage) {}
    absl::optional<uint64_t> rateKbps(const Http::RequestHeaderMap*) const override {
      return fixed_rate_kbps_;
    }

    envoy::type::v3::FractionalPercent percentage(const Http::RequestHeaderMap*) const override {
      return percentage_;
    }

  private:
    const uint64_t fixed_rate_kbps_;
    const envoy::type::v3::FractionalPercent percentage_;
  };

  // Rate limit provider that reads the rate limit from an HTTP header.
  class HeaderRateLimitProvider : public RateLimitProvider, public HeaderPercentageProvider {
  public:
    HeaderRateLimitProvider(const envoy::type::v3::FractionalPercent& percentage)
        : HeaderPercentageProvider(HeaderNames::get().ThroughputResponsePercentage, percentage) {}
    // RateLimitProvider
    absl::optional<uint64_t> rateKbps(const Http::RequestHeaderMap* request_headers) const override;
    envoy::type::v3::FractionalPercent
    percentage(const Http::RequestHeaderMap* request_headers) const override {
      return HeaderPercentageProvider::percentage(request_headers);
    }
  };

  using RateLimitProviderPtr = std::unique_ptr<RateLimitProvider>;

  RateLimitProviderPtr provider_;
};

using FaultRateLimitConfigPtr = std::unique_ptr<FaultRateLimitConfig>;

} // namespace Fault
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
