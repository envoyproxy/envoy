#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"

#include "common/common/macros.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

struct SamplingRequest {
  std::string host_;
  std::string http_method_;
  std::string http_url_;
};

/**
 * Strategy provides an interface for implementing trace sampling strategies.
 */
class SamplingStrategy {
public:
  explicit SamplingStrategy(Random::RandomGenerator& rng) : rng_(rng) {}
  virtual ~SamplingStrategy() = default;

  /**
   * sampleRequest determines if the given request should be traced or not.
   * Implementation _must_ be thread-safe.
   */
  virtual bool shouldTrace(const SamplingRequest& sampling_request) PURE;

protected:
  uint64_t random() const { return rng_.random(); }

private:
  Random::RandomGenerator& rng_;
};

using SamplingStrategyPtr = std::unique_ptr<SamplingStrategy>;

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
