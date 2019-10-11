#pragma once

#include <memory>
#include <random>

#include "envoy/common/pure.h"

#include "common/common/macros.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

struct SamplingRequest {
  /**
   * Creates a new SamplingRequest
   *
   * @param host_name The host name the request.
   * @param http_method The http method of the request e.g. GET, POST, etc.
   * @param http_url The path part of the URL of the request.
   * @param service The name of the service (user specified)
   * @param service_type The type of the service (user specified)
   */
  SamplingRequest(absl::string_view host_name, absl::string_view http_method,
                  absl::string_view http_url)
      : host_(host_name), http_method_(http_method), http_url_(http_url) {}

  const std::string host_;
  const std::string http_method_;
  const std::string http_url_;
};

/**
 * Strategy provides an interface for implementing trace sampling strategies.
 */
class SamplingStrategy {
public:
  explicit SamplingStrategy(uint64_t rng_seed) : rng_(rng_seed) {}
  virtual ~SamplingStrategy() = default;

  /**
   * sampleRequest determines if the given request should be traced or not.
   */
  virtual bool sampleRequest(const SamplingRequest& sampling_request) {
    UNREFERENCED_PARAMETER(sampling_request); // unused for now
    return rng_() % 100 == 42;
  }

private:
  std::mt19937 rng_;
};

using SamplingStrategyPtr = std::unique_ptr<SamplingStrategy>;

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
